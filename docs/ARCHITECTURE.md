# VoIPModem — Architecture

This document describes what is implemented today, not the eventual
target. Requirements are in `PROJECT_SPEC.md`; phase status and test
results are in `ROADMAP.md`. Update this file whenever module boundaries
or responsibilities change.

## Overview

VoIPModem is a modem bank in software. Each configured line is one
modem: a Hayes-compatible serial port on one side (a com0com virtual
port that Windows' own modem driver and RAS talk to), and a SIP
extension on the other. When a call is up, the call's G.711 audio is
run through a real softmodem, libv90modem, which does V.8 and the
modulation (V.21 up to V.90/V.92), and V.42 error control with
V.42bis/V.44 compression. The bytes that come out are passed to the
serial port, and the bytes from the serial port go out over the modem
link.

```
 Windows RAS / terminal                                Asterisk  <-->  far modem
        |                                                 ^
   com0com pair                                           | SIP + RTP (G.711)
        |                                                 |
  [ serial / Hayes ] -- [ modem state machine ] -- [ SIP call ] -- [ call audio <-> libv90modem + V.42 ]
```

The layers are kept apart, as the spec requires: SIP call establishment,
the modem link (libv90modem reaching data mode) and PPP (Windows RAS,
later) are separate states, and success at one is never taken as success
at another. `CONNECT` is reported only when the modem link is up.

## Module layout

```
src/
  Lib/                     VoIPModemLib (static library)
    Core/                  Modem state machine, result codes, ICallController seam
    Hayes/                 AT command parsing
    Serial/                Serial port abstraction + Win32 implementation
    Sip/                   PjsipEndpoint, PjsipCallController, CallAudioBridge (the modem)
    Audio/                 WindowsAudioOutput: optional local "modem speaker"
    Dsp/                   Dial tone/DTMF generators for the local speaker (+ a Goertzel detector their tests use)
    Config/                INI loading, AppConfig (one ModemInstanceConfig per line)
    Logging/               Leveled logger + TaggedLogger (per-line prefix)
  App/                     VoIPModem (executable)
    Application.*          Loads config, owns the shared Logger + PjsipEndpoint, runs all lines
    ModemInstance.*        One modem line: port, call controller, state machine, session, thread
    CommandLineSession.*   Line buffering, local echo, response framing
    main.cpp               Entry point, Ctrl+C handling
tests/                     VoIPModem.Tests (executable, hand-rolled framework in TestFramework.h)
third_party/
  pjproject/               PJSIP 2.17, git submodule, built from source
  pjsip-config/            VoIPModem's config_site.h for pjproject
  libv90modem/             libv90modem 0.6.0, git submodule (GPL-2.0-only)
  libv90modem-build/       libv90modem.vcxproj: MSVC build of the submodule
build/
  VoIPModem.props          Imported by every project: output folders, PJSIP paths and library name
scripts/
  setup-pjproject.ps1      One-time setup: init the pjproject submodule + place config_site.h
```

All modem signal processing is libv90modem's; VoIPModem has no DSP of
its own beyond those local-speaker sounds.

## Dependency direction

```
main.cpp -> Application -> ModemInstance (one per line) -> CommandLineSession -> ModemStateMachine (Core)
                        \-> PjsipEndpoint (shared)      \-> WindowsSerialPort -> AtCommandParser (Hayes)
                                                         \-> ICallController (Core)
                                                               implemented by: NullCallController
                                                                             | PjsipCallController -> CallAudioBridge -> libv90modem
```

`ModemStateMachine` depends only on `ICallController`; it knows nothing
about serial ports, PJSIP or the modem library. `NullCallController`
(no SIP configured: `ATD`/`ATA` honestly fail) and `PjsipCallController`
are the two implementations.

## Modem lines (ModemInstance)

Each configured line is a `ModemInstance` with its own
`WindowsSerialPort`, `ICallController`, `ModemStateMachine` and
`CommandLineSession`, running its own loop on its own thread. Lines share
only the `Logger` (through a `TaggedLogger` that prefixes each line's
messages, e.g. `[Line1]`) and the process-wide `PjsipEndpoint`.

Config: a bare `[Modem]` section is one unnamed line; `[Modem.<name>]`
sections are N named lines. If one line's port fails to open, it is
logged and skipped and the other lines still start.

The loop: in command mode, serial bytes go through `CommandLineSession`
to the parser and state machine. In `OnlineData`, serial bytes go
straight to `ICallController::SendData`, except for the Hayes escape
sequence (`+++` with 1 s of guard time before, between and after), which
returns to command mode with the call still up, so `ATH` can end it.
Received bytes from `PollReceivedData()` are written to the port.
`PollEvent()` is polled every iteration and routed through
`ModemStateMachine::HandleCallEvent()` to produce `RING`,
`CONNECT <rate>`, `NO CARRIER`, `BUSY` or `NO ANSWER`. `RING` repeats every
3 s while a call alerts, and S0 auto-answer runs `ATA` after S0 rings.

## Hayes command set

- **Parser** (`AtCommandParser`): `AT`, `ATI`, `ATZ`, `ATE`, `ATV`, `ATQ`,
  `ATS<n>=<v>`, `ATS<n>?`, `ATD`/`ATDT`/`ATDP` (the T/P modifier is
  stripped before dialling), `ATA`, `ATH`, `AT&F`, chained commands
  (`ATE1V0Q0`), and V.25ter extended commands `+NAME`/`#NAME` with
  `=?`, `?` and `=<value>`. Anything else is `ERROR`.
- **Extended commands answered**: `+FCLASS` (data only: `0`), `+GMI`,
  `+GMM`, `+GMR`. `+GCI` and `#CLS` return `ERROR`; neither is modelled.
  This is enough for Windows' "Standard Modem Types" driver and its
  "Query Modem" diagnostic.
- **State machine** (`ModemStateMachine`): the E/V/Q profile, ten S
  registers (S0 seeded from `AutoAnswerRingCount`), and the state
  (`CommandMode`, `Dialing`, `Answering`, `OnlineData`, ...).
  `ATD`/`ATA` start an attempt and return no result; the result arrives
  later through `HandleCallEvent()`, as on a real modem.
- **Result codes**: verbose or numeric (`ATV`), suppressed with `ATQ1`.
  `CONNECT` carries the rate the modem receives at in verbose mode
  (`CONNECT 26400`); numeric mode sends `1`. Responses are framed
  `\r\n<text>\r\n` in both modes.

## SIP (PjsipEndpoint, PjsipCallController)

PJSIP (pjproject 2.17) is a git submodule built from source through its
own `pjproject-vs14.sln`, target `libpjproject` (the aggregate of pjlib,
pjmedia, pjnath, pjsip, pjsua2 and codecs), with `PlatformToolset`
overridden to `v145`. `scripts/setup-pjproject.ps1` copies
`third_party/pjsip-config/config_site.h` into place first, since
pjproject requires but does not track it.

- **One endpoint per process** (`PjsipEndpoint`, owned by `Application`,
  started when `[SIP] LocalPort` is nonzero): pjsua2 allows one
  `Endpoint` per process, so the UDP transport is shared. Each line with
  a `SipServer` gets its own `PjsipCallController` and its own registered
  account (extension).
- **Async calls**: `Dial()`/`Answer()`/`HangUp()` are fire-and-forget;
  outcomes arrive through `PollEvent()`. PJSIP callbacks run on PJSIP's
  thread, so `PjsipCallController::Impl` guards its event queue and call
  pointers with a mutex, and a disconnected `pj::Call` is retired to a
  zombie list and destroyed on the next `PollEvent()`, never inside its
  own callback. Threads other than the one that started the endpoint call
  `PjsipEndpoint::RegisterCurrentThread()` first.
- **Call events**: SIP answer is not `CONNECT`. `PollEvent()` reports
  `Connected` (with the rate) when the call's modem reaches data mode.
  When the modem ends (no answer tone, nothing in common, carrier lost,
  cleardown, or V.42 failing), it hangs up the SIP call and reports
  `NO CARRIER` once; the SIP disconnect that follows is not reported a
  second time. SIP failures map to Hayes codes in
  `MapSipStatusToResultCode`: 486/600/603 to `BUSY`, 480/408/487/504 to
  `NO ANSWER`, everything else to `NO CARRIER`. A second incoming call
  while one is active gets 486 Busy Here.
- **Logging**: PJSIP's own log goes to the shared `Logger` through a
  `pj::LogWriter` (`[SIP]` prefix), at Debug level, including every SIP
  message and jitter-buffer event. `consoleLevel` must equal `level`:
  pjsua only hands the writer messages at or below `consoleLevel`.
  Passwords are never logged.

## Media path: keeping the audio intact

A modem needs the samples to arrive exactly as sent, at a steady rate.
PJSIP's defaults are tuned for voice, which tolerates what a modem does
not. `PjsipEndpoint::Start()` therefore sets:

| Setting | Value | Why |
|---|---|---|
| Codecs | PCMU and PCMA only; all others disabled | a voice codec destroys modem signals |
| Clock rate | 8000 Hz (and sound clock) | no resampling anywhere |
| VAD | off | a steady carrier is not silence |
| Echo canceller | off (`ecTailLen = 0`) | the modems cancel their own echo |
| PLC (G.711) | off | a missing frame must stay silence, not invented audio |
| Jitter buffer | init 160 ms, min/max prefetch 120/400 ms, max 1000 ms, discard NONE | never run empty, never drop frames to cut latency |
| Timer resolution | `timeBeginPeriod(1)` | with no sound card, the null device's clock drives the conference bridge and must tick steadily |

The jitter buffer and PLC settings matter most. With PJSIP's defaults (no
prefetch, progressive discard, PLC on), the receive buffer ran empty
every few seconds and each event invented or dropped 20 ms of audio. A
V.34 receiver loses lock on any one of them, so the link died seconds
into data mode. The added delay only lengthens the round trip, which
the modems measure during training.

No sound device is needed: `audDevManager().setNullDev()` gives the
conference bridge its clock. Each call's audio is connected to the
line's `CallAudioBridge` and nothing else.

## The modem (CallAudioBridge + libv90modem)

[libv90modem](https://github.com/georgica/libv90modem) 0.6.0 (C99,
fixed point, GPL-2.0-only) is the modem: V.8, V.21, V.22/V.22bis,
V.32bis, V.34, V.90/V.92 (answering = digital/server side, calling =
analogue/client side), Bell 103/212A, and V.42 (LAPM) with V.42bis/V.44
on its bit pump, with fallback to plain start-stop characters when the
far end has no error control. It takes the samples the line brought and
returns the samples to send. Upstream ships only a GNU Makefile;
`third_party/libv90modem-build/libv90modem.vcxproj` builds the same
sources (`api/`, `modem/`, `v90/`, `v34/`, `v32/`, `v92/` and six `v42/`
files) as a C11 static library with MSVC v145. It compiles cleanly
(two warning classes from upstream's GCC pragmas and an intended shift
disabled).

`CallAudioBridge` (src/Lib/Sip) is a `pj::AudioMediaPort` attached to
the call's audio when PJSIP reports media active:

- **Per call**: one `v90m` modem (role `ORIGINATE` when we dialled,
  `ANSWER` when we answered; the codec's law, PCMU or PCMA, from the
  negotiated stream) and one V.42 endpoint (`v90m_ec`).
- **Per 20 ms frame**, on PJSIP's media thread, inside `onFrameReceived`:
  the 160 received samples go through `v90m_process_linear`, which
  produces the 160 samples sent on the next `onFrameRequested`. pjmedia's
  16-bit PCM is already on the scale libv90modem uses (V.90 Table 1;
  0 dBm0 = RMS 15770), so no scaling happens. Then the V.42 endpoint is
  ticked, queued serial bytes are written into it, and received bytes are
  read out of it.
- **Bits**: the modem's `getbit`/`putbit` callbacks go to the V.42
  endpoint once the modem is connected (mark before that).
- **Events**: on `V90M_EV_CONNECTED` the bridge logs mode, rates, SNR and
  round trip, resets the V.42 endpoint for this call and flags the
  connect for `PollEvent()`. Data stopped/moving (retrains, rate
  renegotiations) is passed to V.42. `V90M_EV_ENDED`, or V.42 reaching
  `DISCONNECTED` while the modem is up (which clears the modem down),
  flags the end.
- **V.42 detection is always on when answering**, even when libv90modem
  reports `odp_bypass` (both ends V.92 with LAPM in V.8, where V.92 9.3.1
  lets the caller skip detection). With detection on, the answerer still
  starts LAPM on a caller's flags or SABME without an ODP, so it serves
  callers that skip detection and callers that run it. With it off, it
  serves only the first kind.
- **Threading**: the modem, the V.42 endpoint and their callbacks run only
  on the media thread. `EnqueueTxByte`, `DrainRxBytes`,
  `TakeModemConnected` and `TakeModemEnded` are the thread-safe surface
  used from the line's thread. Frame processing and destruction are
  serialized by a mutex so a frame in flight finishes before the modem is
  freed.
- **Modulations** offered come from the line's `Modulations=` setting
  (default: libv90modem's defaults plus V.92).
- **Local speaker** (`EnableLocalAudio=1`): each frame's RX and TX are
  mixed to the PC's default output through `WindowsAudioOutput`, and
  `ModemInstance` plays a dial tone and the DTMF digits when dialling,
  like a modem speaker. It has no effect on the call.

## Configuration keys

Top level: `[Logging] Level`, `File`; `[SIP] LocalPort`. Per line:
`ComPort`, `BaudRate`, `Identification`, `AutoAnswerRingCount`,
`Modulations`, `EnableLocalAudio`, `SipServer`, `SipExtension`,
`SipPassword`, `SipRealm`. See `config/voipmodem.ini.sample`.

## Windows integration

Windows does not see a bare com0com port as a modem. Install the
generic "(Standard Modem Types)" driver (Device Manager, Add legacy
hardware, Modem) on the **other** side of each com0com pair from the port
VoIPModem opens (VoIPModem on COM3, driver on COM4). Check the pairing
with com0com's `setupc.exe list` (run elevated); pairs are not always
adjacent numbers. RAS is then configured against that modem; VoIPModem
does no PPP itself (see `PROJECT_SPEC.md` and the roadmap's Phase 5).

## Known limitations

- **V.92 PCM upstream** has not been reached against the one far modem
  tested; calls run at V.90. As the caller, a full V.92 offer has ended
  in a cleardown from our side in Phase 4. See `ROADMAP.md`.
- **Calling a modem that opens with V.8bis**: libv90modem's calling side
  waits a fixed 10 s for the answer tone (not exposed in its API). A far
  modem that spends longer on V.8bis before ANSam is missed.
- **No call-audio recording** on our side yet, so a failed call can only
  be diagnosed from logs and the far end's capture.
- **Escape sequence**: online data mode passes every byte to the modem
  except a valid `+++`; there is no `S2`/`S12` configurability.
- **Ctrl+C shutdown** with several lines and calls up has only been
  exercised by killing the process.
- `ATA`/`ATH` take no digit suffix; a command line over 256 characters is
  truncated; `RING` is a fixed 3 s cadence; V0 responses end in `\r\n`.
