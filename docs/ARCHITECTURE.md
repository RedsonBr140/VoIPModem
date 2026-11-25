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

VoIPModem runs headless, in a console or as a Windows service, and is
operated through a **control API** (JSON lines over TCP, localhost by
default; `CONTROL_API.md`). **VoIPModem Manager** (`VoIPModemManager.exe`)
is a separate Win32 program that only talks to that API, so the service
runs without it, and it can watch a service on another computer.

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
    Config/                INI loading, AppConfig (one ModemInstanceConfig per line), IniDocument (writes the ini, keeping comments)
    Control/               ControlServer (the API's TCP transport), LineStatus, ServiceStats, SystemInfo (COM ports, com0com pairs, modem drivers)
    Modem/                 LoopbackTest: the self-test (two libv90modem modems back to back)
    Logging/               Leveled logger (file rotation, in-memory tail for the API) + TaggedLogger
  App/                     VoIPModem (executable)
    Application.*          Loads config, owns Logger + PjsipEndpoint + ControlServer, runs and reconfigures the lines, answers the API
    ModemInstance.*        One modem line: port, call controller, state machine, session, thread; status and call bookkeeping
    CommandLineSession.*   Line buffering, local echo, response framing
    main.cpp               Entry point: console (Ctrl+C), Windows service, --install / --uninstall
  Manager/                 VoIPModemManager (Win32 GUI; no PJSIP, no modem engine)
    MainWindow.cpp         Main window: lines list, statistics, carrier, event log, status bar
    Dialogs.*              Connect, line properties / new line, settings, self-test, ports, SIP trace, AT console, about
    ControlClient.*        The API client, on a worker thread
tests/                     VoIPModem.Tests (executable, hand-rolled framework in TestFramework.h)
third_party/
  pjproject/               PJSIP 2.17, git submodule, built from source
  pjsip-config/            VoIPModem's config_site.h for pjproject
  libv90modem/             libv90modem 0.6.0, git submodule (GPL-2.0-only)
  libv90modem-build/       libv90modem.vcxproj: MSVC build of the submodule
  nlohmann/                JSON for Modern C++ 3.11.3, single header (MIT), for the control API
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
                        \-> ControlServer               \-> ICallController (Core)
                        \-> ServiceStats <--------------/     implemented by: NullCallController
                                                                             | PjsipCallController -> CallAudioBridge -> libv90modem

VoIPModemManager --TCP--> ControlServer (only this; it links no VoIPModem library)
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
sections are N named lines. A line's SIP account is created and
registered only once its COM port is open. If a port fails to open, the
line shows "Port closed" and `Application` tries again every 30 s; the
other lines run regardless. There is no default line or port: an ini
without `[Modem...]` sections runs no lines (the control API still runs,
so lines can be added from the Manager), and a section without
`ComPort=` shows "No COM port" and opens nothing. `InService=0` keeps a line unregistered (its
port stays open).

Each line keeps a status snapshot (refreshed twice a second: state,
caller, modulation and rates, SNR, retrains, V.42 errors, data bytes,
RTP jitter and loss, registration) and reports its calls to
`ServiceStats` (offered, answered, trained, failed to train, refused
busy, caller hung up first, durations, connections per mode). Operator
actions from the API (answer, hang up, in/out of service, re-register)
are queued to the line's own thread, which owns the call. A refused
(un)registration (PJSIP busy with a REGISTER in flight) is retried every
2 s until PJSIP takes it.

The loop: in command mode, serial bytes go through `CommandLineSession`
to the parser and state machine. In `OnlineData`, serial bytes go
straight to `ICallController::SendData`, except for the Hayes escape
sequence (`+++` with 1 s of guard time before, between and after), which
returns to command mode with the call still up, so `ATH` can end it.
Received bytes from `PollReceivedData()` are written to the port.
`PollEvent()` is polled every iteration and routed through
`ModemStateMachine::HandleCallEvent()` to produce `RING`,
`CONNECT <rate>`, `NO CARRIER`, `BUSY` or `NO ANSWER`. `RING` repeats every
3 s while a call alerts, and S0 auto-answer runs `ATA` after S0 rings
(not while the DTE's DTR is low, unless `&D0`; see "Serial control
signals").

## Hayes command set

- **Parser** (`AtCommandParser`): `AT`, `ATI`, `ATZ`, `ATE`, `ATV`, `ATQ`,
  `ATS<n>=<v>`, `ATS<n>?`, `ATD`/`ATDT`/`ATDP` (the T/P modifier is
  stripped before dialling), `ATA`, `ATH`, `AT&F`, `ATL`, `ATM`, `AT&C`,
  `AT&D`, `AT&K`, `ATX`, `ATN`, chained commands (`ATE1V0Q0`), and
  V.25ter extended commands `+NAME`/`#NAME` with `=?`, `?` and
  `=<value>`. A leading run of `+` before `AT` is skipped (see below).
  Anything else, including an out-of-range value for one of the
  commands listed under "Windows Unimodem init strings", is `ERROR`.
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

### Windows Unimodem init strings

Windows' generic modem driver (`mdmgen.inf`, "Standard 56000 bps Modem")
sends `AT&FE0V1&C1&D2S95=47S0=0` and `ATS7=60L0M1&K3N1X4` when RAS opens
the line, and closes the line if either returns `ERROR`. Both now return
`OK`. Each value goes into `ModemProfile` (reset by `AT&F` and `ATZ`).
`&C` and `&D` are **honored** (see "Serial control signals" below):

| Command | Accepted | Factory | What it does |
|---|---|---|---|
| `AT&Cn` | 0-1 | 1 | DCD always on (`&C0`), or only while the modem data link is up (`&C1`) |
| `AT&Dn` | 0-3 | 2 | On a DTR drop: ignore, online command mode, hang up, hang up and reset |

The rest are **accepted and stored, not honored**: changing them changes
nothing about how the modem behaves.

| Command | Accepted | Factory | What honoring it would mean (not done) |
|---|---|---|---|
| `AT&Kn` | 0, 3-6 | 3 | Flow control: none, RTS/CTS, XON/XOFF, transparent XON/XOFF, both |
| `ATXn` | 0-4 | 4 | Choose the result-code set; today the full set is always used |
| `ATNn` | 0-1 | 1 | Fixed rate (S37) or negotiate; today `Modulations=` alone decides |
| `ATLn`, `ATMn` | any digit | 2, 1 | Physical speaker volume and mode (no such speaker) |

`S7=60` (carrier wait time) is stored and not acted on. `S95=47`
(Rockwell's extended result-code register) returns `OK` but is discarded,
as is any write above `S9`; reading it back gives `000`.

When a call has already ended, Unimodem still sends `+++`, waits, and
sends `ATH<cr>`. In command mode the `+` bytes are ordinary line
characters, so the line reads `+++ATH`. The parser skips a leading run of
`+` when `AT` follows it, so that line runs as `ATH`. A line of only `+`,
or `+` followed by anything but `AT`, is still `ERROR`.

### Serial control signals

VoIPModem's port is the modem (DCE) end of the line, but a com0com pair
is two DTE-style ports wired back to back: there is no DCD output to
drive. With com0com's **default wiring** each port's DTR drives the other
port's DSR and DCD, and RTS drives the other's CTS. So:

| Signal on the driver's port | Comes from | Means |
|---|---|---|
| DCD | VoIPModem's DTR | Carrier, per `AT&C` |
| DSR | VoIPModem's DTR | Same as DCD (side effect, see below) |
| CTS | VoIPModem's RTS | Always on (no flow control, `&K` not honored) |

| Signal on VoIPModem's port | Comes from | Means |
|---|---|---|
| DSR | The driver's DTR | DTE ready, acted on per `AT&D` |

`WindowsSerialPort` opens the port with `DTR_CONTROL_DISABLE`, so DTR
starts low and moves only through `SetDtr()` (`EscapeCommFunction`
`SETDTR`/`CLRDTR`); RTS stays on. `ReadDsr()` reads `GetCommModemStatus`
`MS_DSR_ON`. Both are long-standing Win32 calls, present on XP and
Server 2003.

The decisions are in `ModemStateMachine`; `ModemInstance` only moves them
to and from the port:

- **Carrier** (`CarrierAsserted()`): `&C0` always; `&C1` only while the
  data link is up, from `CONNECT` until the call ends, including online
  command mode after `+++`. Off at idle, while ringing, dialing and
  training. `ModemInstance` sets DTR to match every loop iteration, and
  straight after each call event, before writing its result, so DCD is
  up before `CONNECT` and down before `NO CARRIER`.
- **DTR drop** (`HandlePeerDtrChange()`): the driver's DTR is polled once
  per loop iteration (at least every 200 ms, the read timeout) and
  debounced by `DtrDebouncer`: a drop counts only after reading low for
  50 ms on two or more samples, so a glitch as either side opens or
  closes is ignored. It is acted on only while a call is in progress:
  dialing, answering after `ATA` or auto-answer, training, online, or
  online command mode. A call that is only ringing is left ringing.
  - `&D0`: ignored.
  - `&D1`: in online data, back to command mode with the call up, `OK`.
    Otherwise nothing.
  - `&D2`: hang up and return to command mode. `NO CARRIER` follows from
    the call controller's `Ended` event, as after `ATH`.
  - `&D3`: as `&D2`, then reset as `ATZ` (with or without a call). Like
    `ATZ`, that sets S0 back to 0, so the line stops auto-answering.
- **Auto-answer** (`AutoAnswerAllowed()`): not while DTR is low, unless
  `&D0`. `RING` is still sent. The level read when the port opens counts
  as a starting point, not a drop.

Known side effects and limits:

- **DSR on the driver side follows DCD.** With the default wiring the
  driver sees DSR off whenever there is no carrier (under `&C1`, that is
  whenever there is no call). Windows' "Standard Modem Types" driver is
  not known to require DSR, but other software that checks DSR before
  talking to a modem will see one that is "not ready" at idle. Use
  `AT&C0` for such software, or remap the driver side's DSR in com0com so
  that it means "VoIPModem's port is open" instead. com0com's `setupc`
  has per-port pin options for this; check `setupc help` on the machine
  for the exact option name and values, which have not been verified for
  this document. Leave DCD on its default (the other port's DTR), or
  carrier stops working.
- **No DTE means no auto-answer.** When nothing has the driver-side port
  open, com0com reports its DTR as off, so under the factory `&D2` an
  incoming call rings without being answered. That is what a real modem
  does; a line that should answer with nothing attached needs `AT&D0`
  (which `ATZ`/`AT&F` undo). A real COM port with no DSR wired behaves
  the same way.
- **Closing the Manager's AT console during a call hangs it up**: the
  console opens the driver side with DTR on and closing it drops DTR.
- RTS/CTS flow control (`&K3`) and the Ring Indicator pin are not
  driven.

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
| Echo canceller | off (`ecTailLen = 0`; `[Media] EchoCanceller=1` turns it on, against advice) | the modems cancel their own echo |
| PLC (G.711) | off | a missing frame must stay silence, not invented audio |
| Jitter buffer | init 160 ms, min/max prefetch 120/400 ms, max 1000 ms, discard NONE (`[Media] JitterBufferMs` scales all four) | never run empty, never drop frames to cut latency |
| Codec order | PCMU first (`[Media] Codec=PCMA` for A-law first) | both are offered; the far end picks |
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
- **Error control** can be turned off per line (`ErrorControl=0`): the
  bits then go through libv90modem's start-stop character framer
  (`v90m_async`) instead of the V.42 endpoint. `Compression=0` keeps
  LAPM but negotiates neither V.42bis nor V.44.
- **Status**: after each frame the bridge copies libv90modem's state and
  statistics (and, twice a second, the V.42 counters) into a snapshot the
  line's thread reads for the control API.
- **Local speaker** (`EnableLocalAudio=1`): each frame's RX and TX are
  mixed to the PC's default output through `WindowsAudioOutput`, and
  `ModemInstance` plays a dial tone and the DTMF digits when dialling,
  like a modem speaker. It has no effect on the call.

## Configuration keys

- `[Logging]`: `Level`, `File` (relative to the ini's folder), `RotateKB`,
  `Keep`, `TraceAT`.
- `[SIP]`: `LocalPort`, `Transport` (UDP/TCP), `Server` (default
  registrar), `Realm`, `RegisterExpiry`, `RegisterOnStart`.
- `[Media]`: `Codec`, `JitterBufferMs`, `RtpPort`, `RtpPortRange`,
  `EchoCanceller`.
- `[Control]`: `Port`, `BindAddress`, `Password`.
- `[Defaults]`: any per-line modem key below; a line that leaves a key
  out takes it from here.
- Per line: `ComPort`, `BaudRate`, `Identification`,
  `AutoAnswerRingCount`, `Modulations`, `ErrorControl`, `Compression`,
  `InService`, `EnableLocalAudio`, `SipServer`, `SipExtension`,
  `SipPassword`, `SipRealm`.

See `config/voipmodem.ini.sample`. The Manager writes the ini through
`IniDocument`, which changes only the keys it sets and keeps every
comment, blank line and unknown key.

## Service and control API

`main.cpp` runs one `Application` in a console, or under the service
control manager (`--service <ini>`; `--install` / `--uninstall` set it
up: automatic start, restart on failure). A service's working directory
is System32, so relative paths in the ini are taken from the ini's own
folder. `service.restart` ends `Application::Run()` and `main` runs a
new one in the same process (PJSIP is torn down and created again).

`Application::Run()`'s thread (the "main" thread, which created the PJSIP
endpoint) owns the list of lines. The `ControlServer` serves each client
connection on its own thread; read-only requests (`status`, `log`,
`system.ports`, `selftest`) answer from snapshots, and anything that
changes lines or the ini is queued to the main thread and waited for.
Changing settings writes the ini and then applies it the same way a
reload does: logging at once; a line whose settings changed is rebuilt
when it has no call (until then its state shows `*`); added and removed
lines start and stop; SIP transport, media and control settings need a
restart, which the API reports (`restartRequired`). Passwords are never
returned (`passwordSet` only) and never logged. The protocol is in
`CONTROL_API.md`.

## VoIPModem Manager

A Win32 program (plain API and common controls, no MFC) laid out after
`docs/design` (the classic Windows Server 2003 look): menu, toolbar,
the lines list, the statistics and negotiation panes, the event log and
a status bar. It polls `status` and `log` once a second on a worker
thread and sends operator actions as API requests; it keeps no modem
state of its own. Dialogs: connect (host, port, password), new line and
line properties, settings (SIP account, audio/RTP, modem defaults,
logging, control API), loopback self-test, com0com pairs and modem
drivers, SIP trace, AT console, about. Two things run on the Manager's
own computer rather than through the API: the AT console (opens a COM
port directly) and File > Start/Install service (the local service
control manager). Its connection target and window layout are kept under
`HKCU\Software\VoIPModem\Manager`; the password is not stored.

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
- **Serial control signals**: DCD and DTR follow `&C`/`&D` (see "Serial
  control signals"); DSR on the driver side follows DCD, and RTS/CTS and
  RI are not driven.
- **Escape sequence**: online data mode passes every byte to the modem
  except a valid `+++`; there is no `S2`/`S12` configurability.
- **Ctrl+C shutdown** with several lines and calls up has only been
  exercised by killing the process.
- `ATA`/`ATH` take no digit suffix; a command line over 256 characters is
  truncated; `RING` is a fixed 3 s cadence; V0 responses end in `\r\n`.
- **Control API**: plain TCP, no TLS. It listens on 127.0.0.1 unless
  `BindAddress` says otherwise; on any other address set `Password`, and
  even then the traffic (password included) is readable on the network.
- **Statistics** are counted since the service started; a restart resets
  them. They are not persisted.
