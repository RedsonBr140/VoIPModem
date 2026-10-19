# VoIPModem — Architecture (Current State)

This document describes what is actually implemented, not the eventual
target architecture. For the full target design and roadmap, see
`PROJECT_SPEC.md` and `ROADMAP.md`. Update this file whenever module
boundaries or responsibilities change — it should never drift from the
real code.

## Module layout

```
src/
  Lib/                     VoIPModemLib (static library)
    Core/                  Modem state machine, result codes, call-controller seam
    Hayes/                 AT command parsing
    Serial/                Serial port abstraction + Win32 implementation
    Sip/                   PJSIP integration (PjsipEndpoint, PjsipCallController)
    Dsp/                   Modem DSP, from scratch (Phase 4) -- see "Modem DSP (Phase 4)" below
    Config/                INI loading, AppConfig (one ModemInstanceConfig per line)
    Logging/               Leveled logger + TaggedLogger (per-instance prefix)
  App/                     VoIPModem (executable)
    Application.*          Loads config, owns the shared Logger + PjsipEndpoint, starts/stops all ModemInstances
    ModemInstance.*        One modem line: its own port/controller/core/session, own thread
    CommandLineSession.*    Byte-level line buffering, local echo, response framing
    main.cpp               Entry point, Ctrl+C handling
tests/                     VoIPModem.Tests (executable)
  TestFramework.h          Hand-rolled registration/assert macros (no external dependency)
  AppConfigTests.cpp
  AtCommandParserTests.cpp
  ModemStateMachineTests.cpp
  FakeCallController.h    Deterministic ICallController test double
third_party/
  pjproject/               PJSIP, vendored as a git submodule pinned to release 2.17
  pjsip-config/            VoIPModem's config_site.h for pjproject (see "SIP integration" below)
scripts/
  setup-pjproject.ps1      One-time setup: init submodule + place config_site.h
```

## Dependency direction

```
main.cpp -> Application -> ModemInstance (one per modem line) -> CommandLineSession -> ModemStateMachine (Core)
                        \-> PjsipEndpoint (Sip, shared)       \-> WindowsSerialPort (Serial) -> AtCommandParser (Hayes)
                                                               \-> ICallController (Core)
                                                                     implemented by: NullCallController | PjsipCallController (Sip)
```

`ModemStateMachine` depends only on `ICallController`, never on
`WindowsSerialPort`, `pjsua2`, or any other SIP type — `PjsipCallController`
(src/Lib/Sip) is just the second implementation of that same interface,
alongside `NullCallController`. `ISerialPort` exists for the same reason
on the serial side, though nothing currently implements it besides
`WindowsSerialPort`.

## Multiple modem instances

Each configured modem line is a `ModemInstance` (src/App), owning its
own `WindowsSerialPort`, `ICallController`, `ModemStateMachine`, and
`CommandLineSession`, and running its own read loop on its own
`std::thread`. Instances do not share any serial, core, or session state
with each other — they only share the top-level `Logger` (via a
`TaggedLogger` wrapper that prefixes each instance's log lines with its
name, e.g. `[Line1]`), which is safe because `Logger::Log` is already
mutex-protected.

`Application` itself owns no serial port, call controller, or state
machine — it only loads config, owns the shared `Logger`, and manages
the `ModemInstance` collection's lifecycle (open each port, start each
thread, wait for a stop request, stop each instance). This mirrors the
`ICallController` seam: a future per-instance SIP registration (Phase 2)
is expected to plug into `ModemInstance` the same way, one extension per
instance, without `Application` needing to change.

Config: `[Modem]` (no suffix) configures exactly one unnamed instance,
preserved for backward compatibility with pre-multi-instance Phase 1
configs. `[Modem.<name>]` — one section per line — configures N named
instances; `<name>` only affects log-line prefixes. `AppConfig.modems`
always has at least one entry (falls back to a single default instance
if the INI defines none). See `config/voipmodem.ini.sample`.

If one instance's `Open()` fails (e.g. a configured COM port doesn't
exist), `Application` logs it and skips that instance rather than
aborting the whole process — the other configured lines still start.
Only if *every* instance fails to open does `Application::Run()` return
a non-zero exit code.

Manually smoke-tested: a two-instance config (`[Modem.Line1]` on COM3,
`[Modem.Line2]` on COM5) opened both ports concurrently and logged
correctly tagged output from both. **Not yet tested**: a terminal
emulator actually exchanging AT commands with two instances at once
(only single-instance command-mode behavior has had that level of
testing so far — see `docs/ROADMAP.md`), and the graceful Ctrl+C
shutdown path specifically under multiple instances (it reuses the same
`Stop()` logic verified for one instance, but hasn't been exercised with
N > 1 by an actual Ctrl+C rather than a forced process kill).

## SIP integration (Phase 2)

PJSIP (pjproject) is vendored as a git submodule at `third_party/pjproject`,
pinned to release **2.17**, and built from source — there is no vcpkg,
Conan, or prebuilt-binary distribution of it, confirmed by checking
both registries and pjproject's own GitHub releases (no binary assets).
It's built via its own `pjproject-vs14.sln` (Visual Studio project files
are PJSIP's officially documented Windows build path; CMake support is
still explicitly experimental and not validated for Windows upstream as
of 2.17), targeting the `libpjproject` project specifically — not
`pjsua2_lib` alone, which has zero `ProjectReference`s and only contains
pjsua2's own wrapper code. `libpjproject` is the aggregate target
(`LinkLibraryDependencies=true`) that actually bundles pjlib, pjlib-util,
pjmedia(+codecs), pjnath, pjsip, pjsua, pjsua2, and the third-party
codecs (speex, GSM, iLBC, G.722.1, SRTP, resample, WebRTC AEC, ...) into
one combined static lib per config:
`third_party/pjproject/lib/libpjproject-x86_64-x64-vc14-{Debug,Release}-Static.lib`.
`PlatformToolset` is overridden to `v145` at the MSBuild command line the
same way as VoIPModem's own projects — pjproject's checked-in project
files target the much older `v140`.

**Setup**: run `scripts/setup-pjproject.ps1` once after cloning (it inits
the submodule if needed and copies `third_party/pjsip-config/config_site.h`
into `third_party/pjproject/pjlib/include/pj/config_site.h`, which
pjproject's build requires but deliberately does not track in git —
same pattern as upstream's own CI, which copies `config_site_test.h` into
place; see `third_party/pjproject/CLAUDE.md`, pjproject's own agent
instructions, for confirmation). Then build `third_party/pjproject/pjproject-vs14.sln`
(target `libpjproject`, configs `Debug-Static`/`Release-Static`, platform
`x64`) before building `VoIPModem.sln` — `VoIPModemLib` only has the
pjproject *include* paths; `VoIPModem`/`VoIPModem.Tests` are the ones
that link the combined `.lib` plus `ws2_32.lib;wsock32.lib;ole32.lib;dsound.lib`.
`PJ_WIN32` needs no explicit project define — pjproject's own
`pjlib/include/pj/config.h` derives it from MSVC's always-predefined
`_WIN32`/`_WIN64`, contrary to some older PJSIP docs pages.

**Architecture**: there is exactly one process-wide `PjsipEndpoint`
(owned by `Application`, started only if `[SIP] LocalPort` is configured
and nonzero), wrapping pjsua2's `Endpoint` — pjsua2 only supports one
`Endpoint` per process, so the UDP transport/port is necessarily
process-wide, not per-instance. Each `ModemInstance` whose config has a
`SipServer` gets its own `PjsipCallController`, which registers its own
SIP account (one extension) against that shared endpoint — multiple
instances share one UDP port but have independent registrations/calls,
the same way a real multi-line ATA or PBX extension set works. An
instance with no `SipServer` configured still gets `NullCallController`,
unchanged from Phase 1.

**Async call model**: `ICallController::Dial()`/`Answer()` are
fire-and-forget (changed from Phase 1's synchronous stub-only shape) —
real SIP signaling resolves later, sometimes seconds later, on PJSIP's
own worker thread, never synchronously on the calling thread. The
eventual outcome arrives via `PollEvent()` (non-blocking), which
`ModemInstance` polls once per read-loop iteration (~every send/receive
cycle) and routes through `ModemStateMachine::HandleCallEvent()` to
produce the actual unsolicited `CONNECT`/`NO CARRIER`/`BUSY`/`NO ANSWER`
response — matching how a real modem behaves (it doesn't print a result
on the same line as the echoed `ATD` either). `CallEventType::IncomingCall`
doesn't produce a response by itself; `ModemInstance::PumpIncomingCallRinging()`
drives the actual `RING` cadence (every `kRingInterval` = 3s) and S0
auto-answer counting, calling `CommandLineSession::TriggerAutoAnswer()`
(which just runs `"ATA"` through the normal command path) once the ring
count reaches the configured `S0` value.

**PJSIP callback thread-safety**: `pj::Account`/`pj::Call` callbacks
(`onRegState`, `onIncomingCall`, `onCallState`) fire on PJSIP's own
worker thread, never on the `ModemInstance` thread that calls
`Dial()`/`Answer()`/`HangUp()`/`PollEvent()`. `PjsipCallController::Impl`
guards its event queue and call pointers with a mutex. A `pj::Call`
object is never deleted from inside its own `onCallState` callback (a
documented pjsua2 hazard — see the nested-class comments in
`PjsipCallController.cpp`); disconnected calls are moved into a
"zombies" list instead and actually destroyed on the next `PollEvent()`,
always safely outside the originating callback's stack. Any thread that
calls into pjsua2 other than the one that called `PjsipEndpoint::Start()`
must call `PjsipEndpoint::RegisterCurrentThread()` first — `ModemInstance::RunLoop()`
does this once at thread start.

**SIP status -> Hayes result code mapping** (`MapSipStatusToResultCode`
in `PjsipCallController.cpp`) is a deliberately small, documented
approximation, not a precise protocol mapping: 486/600/603 -> `BUSY`;
480/408/487/504 -> `NO ANSWER`; everything else (including a normal
hangup after connecting) -> `NO CARRIER`, matching how a real modem
reports a connection that never came up or that dropped, for whatever
reason — real modems don't have a result code for every SIP failure
mode either.

**PJSIP's own internal logging** (SIP dialogs, transport events, auth)
is routed into the shared `Logger` via a `pj::LogWriter` subclass
(`LoggerBridge`, private to `PjsipEndpoint.cpp`), tagged `[SIP]`, per
CLAUDE.md's "structured logging for... SIP dialogs" rule. In practice a
handful of very early pjlib bootstrap messages (e.g. "pjlib 2.17 for
win32 initialized") still print directly to the console before the
custom writer is attached — cosmetic, not a functional gap, since every
message that matters (registration result, call state) does go through
`LoggerBridge`.

**Verified against a real Asterisk test server** (2026-10-13):
registration succeeded (`SIP registration result: 200 OK`) for a real
extension, and `ATD` to an unroutable number dialed through the real
SIP stack and correctly produced `NO CARRIER` on the actual serial port
after the async round-trip — not a mock, and not just a unit test.
A real incoming call was also tested live: RING generation and `ATA`
both worked. Auto-answer initially did not — the cause was a real bug,
not just an unverified path: `ModemInstanceConfig::autoAnswerRingCount`
was parsed from `AutoAnswerRingCount=` but never written into the S0
register anywhere, so it silently had no effect unless `ATS0=n` was
typed manually first. Fixed via `ModemStateMachine::InitializeAutoAnswerRegister()`,
called from `ModemInstance`'s constructor. **Phase 2 is now fully
verified**: re-tested live after the fix — auto-answer works, and two
simultaneously-registered instances (extensions 1003 and 1004, same
Asterisk server) both reached `CONNECT` on real calls. Not yet
verified: the graceful Ctrl+C shutdown path now that it has to tear down
`PjsipCallController`/`PjsipEndpoint` too (only forced-process-kill
shutdown has been exercised with SIP active so far) — a pre-existing gap
carried over from Phase 1/multi-instance, not something Phase 2 made
worse.

## RTP audio (Phase 3)

Phase 2's calls connected (`CONNECT`) but carried no media —
`PjsipEndpoint::Start()` called `audDevManager().setNullDev()`
specifically to avoid touching a host audio device before audio
transport was the thing being built and tested. Phase 3 replaces that:

- **Media config** (`PjsipEndpoint::Start()`): `clockRate`/`sndClockRate`
  both set to 8000 (matches G.711 natively — no resampling anywhere in
  the chain), `noVad = true` (a steady modem carrier is never "silence";
  VAD/silence suppression would clip real data), `ecTailLen = 0`
  (disables echo cancellation). These three map directly to
  `docs/PROJECT_SPEC.md` section 4.D's instruction to "disable or avoid
  audio processing that corrupts modem tones". There is no separate AGC
  toggle in pjsua2's `MediaConfig` to disable explicitly — if automatic
  gain control later proves to be a problem for modem tones, that needs
  digging into `pjmedia_snd_port`-level options directly, since pjsua2
  doesn't expose one.
- **Codec restriction** (`PjsipEndpoint::RestrictCodecsToG711()`, called
  once after `libStart()`): every codec except `PCMU`/`PCMA` (G.711) is
  disabled process-wide (`codecSetPriority(..., 0)`) — per
  `docs/PROJECT_SPEC.md` section 4.E, a lossy voice codec (Opus, Speex,
  GSM, iLBC, ...) would corrupt modem tones outright, so none of them
  should ever be negotiable, not just de-prioritized.
- **Audio sink — pivoted mid-phase, twice** (`SipCall::onCallMediaState`
  in `PjsipCallController.cpp`): originally wired each call's audio
  bidirectionally to this machine's default sound device, chosen (after
  discussing the tradeoff with the user) over a stubbed PCM-frame seam
  because it's the most directly, byear-verifiable proof that G.711
  negotiation/RTP transport/codec fidelity are correct, without blocking
  Phase 4 (which just takes over the same `onCallMediaState` connection
  point). Live-testing that version against a real call immediately
  surfaced `PJMEDIA_EAUD_NODEFDEV` ("unable to find default audio
  device") on **both** playback and capture, even though this machine
  has a working speaker (confirmed via `Get-PnpDevice -Class
  AudioEndpoint`, status OK). Traced into pjproject's own source
  (`pjmedia/src/pjmedia/audiodev.c`, `pjmedia/src/pjmedia-audiodev/wmme_dev.c`,
  `pjsip/src/pjsua2/media.cpp`) to the real cause: this machine has
  **zero** capture devices at the Win32 level
  (`waveInGetNumDevs() == 0`, confirmed directly via a `winmm.dll`
  P/Invoke check — not inferred), and every sound-device path pjsua2
  exposes — both the "default device" lookup and the alternate
  `ExtraAudioDevice` class — hardcodes `PJMEDIA_DIR_CAPTURE_PLAYBACK`
  (duplex only; no playback-only mode exists anywhere in pjsua2's audio
  device layer). This is a genuine PJSIP/hardware constraint on this
  specific machine, not a VoIPModem bug, and not fixable by better error
  handling — no amount of catching exceptions makes a duplex-only API
  open on a machine with no capture device.
- **Current approach**: each call's audio connects to an
  `AudioMediaRecorder` (incoming call audio -> a WAV file,
  `call-recording-<extension>-<unix-timestamp>.wav`, written next to the
  `.exe`) and an `AudioMediaPlayer` (a bundled test tone ->
  `assets/test-tone.wav`, an 8kHz mono 16-bit 1kHz sine tone generated
  for this purpose, looped into the call automatically). Neither needs
  any physical sound device at all, so this works regardless of what
  audio hardware is present. `PjsipEndpoint::Start()` went back to
  `audDevManager().setNullDev()` (same as Phase 2) since nothing needs a
  real device anymore — the null device just gives the conference bridge
  its clock source. `test-tone.wav` is copied next to `VoIPModem.exe` by
  a post-build step in `VoIPModem.vcxproj` (`CopyTestToneAsset` target);
  both the recording and the tone path are resolved relative to the
  running `.exe`'s own directory (`GetModuleFileNameA`), not the current
  working directory, so this works no matter where the process is
  launched from.
- **Verification shape, given this**: confirming Phase 3 now means
  listening to the recorded WAV afterward (not live), and confirming
  whoever is on the physical analog line actually hears the test tone.
  This is a real change in how "bidirectional audio, uncorrupted" gets
  checked for this phase, agreed with the user after the live-device
  path turned out to be unusable on this machine specifically — it may
  still be worth supporting on a machine that does have a capture
  device, but that's not implemented; this is the one and only sink
  right now.

**Verified live** (2026-10-13): a physical analog line, through the
Cisco FXS gateway and Asterisk, dialed into extension 1003 — the call
reached active media, the resulting `call-recording-1003-*.wav` was
clean (not silent, not corrupted), and the test tone was clearly audible
on the analog line's end. This is the first point in the project where
audio has actually flowed end-to-end through the real signal path
described in `docs/PROJECT_SPEC.md` section 2's topology diagram (minus
the Windows-98-client leg, which isn't wired up yet). **Phase 3 is
done.**

What this does *not* prove, carried forward as Phase 4's job: this only
shows a sine tone (or human voice) survives the path cleanly — not that
a real V.34 handshake tone would survive undistorted at the bit
level. That's for a DSP engine to prove, once one exists that can tell
"sounds fine" from "decodes correctly" apart. The WAV recorder/player
seam in `SipCall::onCallMediaState` was exactly where that DSP engine
would plug in — and, as of Phase 4's first wiring increment, it already
has: this sink has been **replaced** by `CallAudioBridge` (see "Modem
DSP (Phase 4)" below). The description above is left as a historical
record of what Phase 3 actually verified live; it is no longer what the
code does.

## Modem DSP (Phase 4)

`src/Lib/Dsp` is a new module boundary, kept separate from `src/Lib/Sip`
for the same reason `ICallController` keeps modem logic separate from
serial I/O: SIP call state, modem DSP state, and (eventually) PPP session
state must stay independently reasoned-about, per `docs/PROJECT_SPEC.md`
section 4 and CLAUDE.md's architectural boundaries. The Dsp module itself
still knows nothing about pjsua2 — the integration lives entirely in
`src/Lib/Sip/CallAudioBridge`, which depends on Dsp's public headers the
same direction `PjsipCallController` already depends on `Core`'s.

**Why building from scratch, not vendoring a library**: see
`docs/ROADMAP.md` Phase 4 for the full library survey (spandsp, slmodemd
via D-Modem, fisher-modem, Microchip's dsPIC30F library, VOCAL) and why
each was rejected. In short: nothing Windows-portable and permissively
licensed has a working V.32bis or V.34 engine today, and the one
implementation that does work (slmodemd) is Linux/x86-32-only, GPL-2 with
a murkier license in later versions, and can't answer calls. The actual
ITU-T V.34/V.8 Recommendations are the implementation reference; D-Modem
is consulted only for architectural ideas, never as a code source, to
avoid pulling its GPL-2 licensing into a project that isn't GPL.

**Current state**: a primitive toolkit only, all operating at 8 kHz (the
same rate `PjsipEndpoint` already negotiates with no resampling, so no
sample-rate conversion will ever be needed between this module and the
call-media seam it will eventually plug into):
- `Dds` -- a phase-accumulator sine/cosine oscillator. Used directly by
  `ToneGenerator` now; will be the carrier source for QAM I/Q modulation
  later, so it already exposes a quadrature (`sin`+`cos`) output even
  though nothing needs the cosine yet.
- `ToneGenerator` -- TX-side pure tones (the V.25/V.8 handshake runs on
  a handful of known single-frequency tones: answer tone, calling tone,
  ANSam).
- `GoertzelDetector` -- RX-side counterpart: a single-bin DFT tone-energy
  estimator, far cheaper than a full FFT when only one or two known
  frequencies matter, which is exactly the handshake-tone case.

6 unit tests (`tests/DspPrimitiveTests.cpp`) cover these: an
exact-value check on `Dds`'s phase sequence at a quarter of the sample
rate (not just a loose floating-point tolerance), and `GoertzelDetector`
correctly distinguishing a tone at its target frequency (~1.0 magnitude)
from a tone elsewhere or silence (~0), including after `Reset()`.

- `FskModulator`/`FskDemodulator` -- continuous-phase binary FSK at a
  configurable mark/space/baud rate. Frequencies and roles are V.21's
  own, confirmed against V.21's frequency table and how V.8 assigns CM
  (calling party) to channel 1 and JM (answering party) to channel 2,
  not assumed: channel 1 mark 980 Hz/space 1180 Hz, channel 2 mark
  1650 Hz/space 1850 Hz, both at 300 baud. Includes V.21/V.8-style async
  byte framing (1 start bit/8 data bits LSB-first/1 stop bit, no
  parity) -- the framing CI/CM/JM octets actually travel in.
  `FskDemodulator` has an explicitly documented limitation: no
  symbol-timing recovery, so it only decodes correctly when the caller
  already knows where bit boundaries start.

5 more unit tests (`tests/FskTests.cpp`, 68 total): bit-level loopback,
byte-level async-framing loopback, two malformed/misaligned-input
cases, and `Fsk_ChunkedDelivery_MatchesWholeBufferDecoding` -- which
matters most: it proves that re-running `DemodulateBits` over a buffer
that grows frame-by-frame (exactly how real call audio arrives, in
160-sample/20ms chunks, not one buffer) decodes identically to one-shot
decoding over the whole thing, *before* any of this touched real call
audio.

**Call-media wiring, increment 1 (superseded) -- `src/Lib/Sip/CallAudioBridge`**:
`SipCall::onCallMediaState`
now creates a `CallAudioBridge` instead of Phase 3's
`AudioMediaRecorder`/`AudioMediaPlayer`. It's a pjsua2 `AudioMediaPort`
subclass -- a custom bidirectional audio port, not a file-backed one --
registered into the conference bridge via `createPort()` and connected
both ways (`callAudio.startTransmit(*this)` for RX,
`startTransmit(callAudio)` for TX), at 8kHz mono 16-bit PCM, 20ms/160-sample
frames (`PJMEDIA_FORMAT_PCM` via `MediaFormatAudio::init`). Per call:
- **TX** (`onFrameRequested`): the answering side (`callingParty == false`)
  generates the ITU-T V.25 answer tone (2100 Hz, via `ToneGenerator`) into
  every outgoing frame; the calling side currently sends silence -- this
  increment only wires up the answerer's half of V.25's tone exchange.
- **RX** (`onFrameReceived`): both sides run a `GoertzelDetector` over
  each incoming frame (reset per-frame, since one frame is already
  exactly one 160-sample Goertzel block) at a configured target
  frequency, and log (via the shared `TaggedLogger`) each time detection
  crosses a magnitude threshold in either direction.
- `callingParty` is threaded from where `SipCall` is actually constructed
  (`PjsipCallController::Dial()` -> `true`; `SipAccount::onIncomingCall()`
  -> `false`), matching the V.8/V.25 calling-party-vs-answerer convention
  these DSP primitives already follow.

**What this honestly proves, and what it doesn't**: this confirms a real
tone generated by this module survives the full SIP/RTP/G.711 path and
can be detected coming back (or from a real peer), using actual call
audio rather than a unit test's synthetic signal -- a legitimate exercise
of the seam. It is explicitly **not** a V.8/V.25 handshake: there is no
negotiation, no state machine, and the RX target frequency
(`kRxProbeTargetHz` = 1100 Hz in `CallAudioBridge.cpp`) is called out in
its own comment as a wiring-verification placeholder, not yet a real V.8
CI/CM signal detector. **No claim of V.32bis or V.34 support exists
anywhere in this codebase**, per CLAUDE.md's rule against claiming a
modem standard is supported before it's implemented and
interoperability-tested. Build-verified (`VoIPModem.sln`, 0
warnings/errors at `/W4`) and unit-test-verified (63/63 passing,
including the 6 DSP primitive tests).

**Live-tested 2026-10-17**: a real phone call, through the Cisco FXS
gateway and Asterisk, into extension 1004 (`[Modem.Line2]` — the only
instance that was up this round; `[Modem.Line1]`/COM4 failed to open,
`GetLastError=5`/Access Denied, a pre-existing environment issue
unrelated to this change and not yet resolved). Auto-answer fired after
2 rings (`ATA`), the call ran ~24 seconds, then was hung up (`ATH`). The
log showed nothing in between those two lines — `CONNECT` itself isn't
logged anywhere (only written to the serial port), and, more
importantly, **`CallAudioBridge::Start()` had no success log of its own
at the time**, so the log alone couldn't confirm whether call media ever
went active or the bridge actually started. The only verification
available was a human on the physical line, who confirmed hearing "a
dial-up tone, that static" during the call — real, human-confirmed proof
that a tone generated by this module survived the full
SIP/RTP/G.711/FXS path to a real phone line. The RX side never logged a
detection, which is expected, not a failure: nothing on an ordinary
phone call transmits a clean tone at the placeholder probe frequency.
**Found and fixed from this test**: added an explicit success log to
`CallAudioBridge::Start()` (role, TX behavior, RX target) so the next
live test doesn't depend solely on someone's ear — exactly the kind of
observability gap CLAUDE.md's "structured logging for... DSP
negotiation" rule exists to catch, and it was only caught by actually
running the test rather than assuming the wiring worked because it
compiled.

`[Modem.Line1]`/COM4's earlier Access Denied turned out to be a stray
PuTTY session sitting on COM4 during the test, not a real bug -- nothing
to fix in code.

**Call-media wiring, increment 2 -- superseding the tone probe above**:
`CallAudioBridge` no longer sends a single tone. Each side now
continuously transmits an incrementing counter byte via `FskModulator`
on its role-correct V.21 channel (calling party: channel 1 TX/channel 2
RX; answering party: channel 2 TX/channel 1 RX) and continuously tries
to decode the other side's bytes back via `FskDemodulator`. Each
decoded byte is logged, flagged as sequential or not (a real decode
error, distinct from "no signal yet"). `CallAudioBridge::Start()` keeps
the success-log fix from increment 1.

**Live-tested and a real bug found and fixed (2026-10-17)**: the first
version of `FskDemodulator` (`DemodulateBits`) required the caller to
keep an ever-growing sample buffer and re-decode it from scratch every
20ms frame -- proven correct in isolation
(`Fsk_ChunkedDelivery_MatchesWholeBufferDecoding`), but once wired into
a real call this turned into unbounded per-frame CPU growth on PJSIP's
media thread: after about 90 seconds, no bytes ever decoded and the
call stopped responding to teardown at all. `FskDemodulator` was
rewritten so `ProcessSamples` carries all bit-boundary state as members
across calls (O(1) amortized per sample, no growing buffer);
`CallAudioBridge` keeps a periodically-compacted `pendingBits_` instead.
Re-tested live: a call stayed healthy (near-zero CPU, flat memory) for
3+ minutes and shut down cleanly on `ATH`/Ctrl+C. Zero `decoded byte`
log lines is expected in that test, not a regression -- the far end was
a plain phone, which has no FSK transmitter of its own.

**A second, distinct bug was then found and deliberately deferred**:
hanging up from the far end (the physical phone, not `ATH`) still does
not tear down the call on our side. Re-confirmed *after* the CPU fix
above, which rules out that fix's theory as the explanation here: CPU
time stayed near zero and memory flat while the call sat stuck, so it
isn't a busy loop. No log line appears at all after `CallAudioBridge
started`, meaning PJSIP's `onCallState` never saw
`PJSIP_INV_STATE_DISCONNECTED` -- most likely because no SIP BYE ever
arrived from Asterisk (an FXS disconnect-supervision issue on the
Cisco gateway/Asterisk side, not confirmed as a VoIPModem bug at all),
but an attempt to verify that via Asterisk's own `core show channels
verbose` was inconclusive because `VoIPModem.exe` was stopped before
the check ran, which tears down our own SIP dialog on exit and makes
"0 channels" meaningless as evidence either way. See `docs/ROADMAP.md`
Phase 4 for the full account. Logged to come back to, not fixed yet --
by explicit user direction, Phase 4 DSP work continued instead.

Build-verified clean (`VoIPModem.sln`, 0 warnings at `/W4`); all 83
tests pass. Still no real symbol-timing recovery: both sides' bit
boundaries are only right because each counts from its own call's
start, not from anything derived from the signal itself.

**V.8 octet-level codec (`src/Lib/Dsp/V8Message`, new)**: the actual
ITU-T Recommendation V.8 (11/2000) text was obtained and read in full
(its PDF, freely downloadable from itu.int) -- clauses 5 and 6 define
exactly how CI/CM/JM/CJ's bits are packed, and this module implements
just that: category/extension octet packing, the call function octet
(including `Data`, the one this project needs), the 3-octet modulation
modes field (V.21 through V.34), PCM modem availability, the protocol
octet (LAPM), the PSTN access octet, the CI/CM/JM preamble bit patterns,
and the CJ terminator. 15 new unit tests assert exact bit patterns
against the spec's own tables (not just round-trips), after a first,
layout-preserving PDF-to-text pass garbled some of Table 3's columns --
a clean re-extraction was used to avoid coding a guessed mapping. This
is pure bit packing: no audio, no sequence assembly, no state machine
yet, the same "primitive in isolation first" discipline as `Dds`/
`GoertzelDetector` before `FskModulator`/`FskDemodulator` existed.

**The full handshake, built on that codec (`V8CallMenu`, `V8Signal`,
`AnsamTone`, `V8Handshake` -- all new, same session)**: `V8CallMenu`
bundles call function + modulation modes + optional categories into one
menu (encode/decode, mode intersection per 7.4, "lowest item number
wins" selection). `V8Signal` builds one CI/CM/JM/CJ bit sequence through
`FskModulator` and finds/reads one back out of a demodulated bitstream.
`AnsamTone` is a new DSP primitive (2100 Hz, 15 Hz AM envelope, 450ms
phase reversals implemented as just negating the sample). `V8Handshake`
is the actual clause 8 state machine for both roles (call DCE: wait for
ANSam, silent Te, CM while waiting for 2 identical JM, send CJ; answer
DCE: initial silence, ANSam while waiting for 2 identical CM, JM while
waiting for CJ) -- CI deliberately skipped (7.1: optional), no
retry/timeout handling, Te/initial-silence fixed at the spec's own
minimums.

**Two real bugs found by this class's own loopback unit test, not code
review** (full account in `docs/ROADMAP.md` Phase 4): (1) receiving
`FskDemodulator`s started ticking their bit-clock long before the other
side's real signal arrived, leaving them phase-misaligned with the
transmitter's actual bit edges (no real symbol-timing recovery exists
in this codebase) -- fixed with a frequency-selective lazy-sync
(`std::optional<FskDemodulator>`, constructed the moment energy at that
channel's own mark/space frequencies first appears, not just any loud
sample, since ANSam's 2100 Hz tone would otherwise trigger a false
sync). (2) Once sync was fixed, every preamble was found but every menu
decode failed, because "not enough data buffered yet" was being treated
the same as "genuine false positive," permanently skipping a real
preamble before its trailing octets had fully arrived -- fixed by only
committing to a decode on a genuine framing failure (the real
end-of-sequence marker), always retrying the same position otherwise.

Result: 8 new tests (105 total) reach `Negotiated` on both sides,
including correct mode-intersection on a mismatch -- but this is a
**lossless, in-process loopback simulation**, not a real call; it
proves the state machine, not survival of a real SIP/RTP/G.711 path.

**Wired into `CallAudioBridge`, superseding the FSK counter-byte
probe**: one `V8Handshake` per call, advertising only V.21 (the one
modulation actually implemented -- not V.32bis/V.34, per CLAUDE.md's
rule against claiming unimplemented standards). Logs the negotiated
mode once reached. Since pjsua2 calls `onFrameReceived`/
`onFrameRequested` separately but `ProcessFrame` wants both at once,
the real work happens in `onFrameReceived` (buffering that frame's TX
output as a side effect), and `onFrameRequested` just drains it.
**Live-tested between two real VoIPModem instances over Asterisk
(2026-10-17)**: reached `Negotiated` on both sides at least once --
the first real call either side of this project has ever fully
negotiated. Found and fixed a real bug on the way (lingering ANSam
energy, delayed by RTP/jitter-buffer latency, was faking out the
receiver's lazy edge-sync; see `docs/ROADMAP.md` Phase 4 for the full
account and the fix, `FeedSyncedDemodulator`'s `competingHz`/
`competingThreshold` gate). But it was **not reliable across repeated
attempts** -- real audio jitter intermittently broke the same
lazy-sync heuristic in other ways (a garbled-but-self-consistent menu
decode on one run, a stalled CM match on another), all symptoms of one
root cause: no real symbol-timing/clock recovery anywhere in this
codebase, just a one-shot "sync to the first loud sample" heuristic
that isn't robust to real-world jitter.

**V.34 Phase 2 handed off from `CallAudioBridge`, opt-in only
(2026-10-19)**: once `V8Handshake` reaches `Negotiated`, if the
selected mode is V34Duplex/V34HalfDuplex, `CallAudioBridge` now
constructs a `dsp::V34Phase2Handshake` and hands the rest of the
call's audio to it (same `ProcessFrame` cadence), logging RTD and L1
milestones as they're reached. Gated behind a new
`ModemInstanceConfig::enableV34Probing` (ini: `EnableV34Probing=1`
under a `[Modem...]` section), default false, which also controls
whether this instance's own V8 menu advertises V34Duplex at all --
**not enabled by default** because a real V.34-capable remote modem
would commit to training this codebase can only follow through
Phase 2 (see `docs/ROADMAP.md` Phase 4), likely hanging or failing the
call instead of gracefully negotiating down to V.21. Intended only
for a controlled test between two of this project's own instances
(e.g. the com0com-paired local test rig), never on an instance taking
real inbound calls. Not yet live-tested over a real call with this
flag on.

**`V34Phase2Handshake` extended to the full error-free Phase 2
procedure (2026-10-19)**: both Tone A/B reversal exchanges, both
directions' L1/L2 transmit and receive, and the INFO1c/INFO1a exchange
that hands off into Phase 3 -- see `docs/ROADMAP.md` Phase 4 for how
the one genuinely ambiguous piece (L2's sender-side "local echo"
duration, a PSTN hybrid-circuit concept with no digital-VoIP
equivalent) was resolved by re-deriving the full event sequence from
the numbered clauses rather than guessing, and for the two real bugs
the extension surfaced and fixed (a desynced detector sample-clock
from inconsistent per-phase feeding; a TX generator referenced before
construction when RX outran TX across a phase boundary). New
`V34Info1.h`/`.cpp` codes INFO1c/INFO1a (Tables 15/16); both sides'
"results" fields are honestly zero/sentinel, since this codebase still
has no trellis/QAM/equalizer and no real L1/L2 channel-response
analysis to report. `V34Phase2Outcome::Phase2Complete` is the ceiling
of what this class now claims.

**Local audio monitoring, "hear the modem" (2026-10-19)**: new
`src/Lib/Audio/WindowsAudioOutput.h`/`.cpp` wraps winmm's
`waveOutOpen`/`waveOutWrite` (multi-buffered, polling `WHDR_DONE`
rather than a callback) to play PCM audio out the Windows default
sound output device. `CallAudioBridge` mixes every frame's RX+TX audio
and writes it there -- but only if `ModemInstanceConfig::enableLocalAudio`
(`EnableLocalAudio=1` per `[Modem...]` ini section) is set for that
line, default false. Per-line and opt-in rather than always-on: two
VoIPModem instances calling each other on the SAME machine (the
com0com test rig) would otherwise both open the shared default device
and mix into one indistinguishable stream, so enable it on just the
one line you want to listen to at a time. There's no separate
modem-specific volume/device setting of its own: the audio is just an
ordinary Windows playback stream, so the *existing* Windows Sound
settings/Volume Mixer already control it.
Separately, `AtCommand`/`AtCommandParser`/`ModemStateMachine`/
`ModemProfile` now accept ATLn/ATMn (speaker volume/mode) instead of
erroring on them -- real-world motivation: Windows' own "Phone and
Modem" control panel maps its Properties dialog's volume slider
directly to ATLn. The two pieces aren't yet connected: ATL/ATM's
values are stored but not read back by the audio monitor (see
`docs/ROADMAP.md` Phase 4 for the honest gap, including why closing it
means crossing the Core/SIP boundary).

**Live-tested with an actual human ear (2026-10-19)**: user confirmed
the V.8 handshake audio sounds clean over the local test rig with
`EnableLocalAudio=1` on one line, then flagged the one real gap: no
dial tone/dialing sound before a call connects, since there's no
physical analog line to produce either from. Added as a deliberately
mocked local cue: new `src/Lib/Dsp/DtmfGenerator.h`/`.cpp` generates
the classic 350+440Hz dial tone and standard-frequency-pair DTMF
digits; `ModemInstance` watches for the transition into
`ModemState::Dialing`, and (if `enableLocalAudio`) plays dial tone
then each digit's DTMF tone on its own `WindowsAudioOutput`, on a
short-lived background thread so the serial-read/call-event loop
isn't blocked for the ~1-3 second sequence. Purely cosmetic -- dialing
itself already happens via the SIP INVITE, not DTMF, so none of this
reaches the far end.

**Real symbol-timing recovery implemented in `FskDemodulator`
(2026-10-18)**: an early-late gate (re-measuring each decided bit's
winning tone in windows shifted slightly earlier/later than where it
was actually placed) feeding a PI loop filter (proportional term for
quick phase correction, integral term for tracking a *sustained* rate
mismatch -- essential, not optional: a proportional-only first attempt
left a nonzero steady-state error against sustained drift and still
desynced). The honest source of drift this tracks: both ends compute
at the same nominal 8kHz with no separate crystals, but a VoIP jitter
buffer's accelerate/expand concealment genuinely plays audio back
very slightly off-rate. Proven in `FskTests.cpp` against a synthetic
0.6%-off-baud stream (verified to actually fail with the loop disabled
first). Re-tested live against the same two-instance setup: 5/5
consecutive dial/redial cycles reached `Negotiated` on both sides with
*correct* `selected mode: V.21` logged on both lines every time -- the
garbled-decode and CM-stall failures above did not recur. This fixes
drift-tracking once a demodulator exists; `V8Handshake`'s own lazy
"sync on first loud sample" heuristic, deciding *when* to construct
one, is unchanged, and genuine carrier/clock recovery at V.32bis/V.34's
actual symbol rates and constellations remains a separate, harder
problem.

Still not done, in the order they're planned (see `docs/ROADMAP.md`
Phase 4 for detail): retry/timeout handling in `V8Handshake`, the
far-end-hangup bug, QAM constellation mapping, the scrambler/
descrambler, carrier recovery, the adaptive equalizer, and every
V.34-specific piece (line probing, shell mapping, trellis coding/
Viterbi decoding, rate negotiation).

## What exists today

- **Hayes parser** (`AtCommandParser`): parses `AT`, `ATI`, `ATZ`, `ATE`,
  `ATV`, `ATQ`, `ATS<n>=<v>`, `ATS<n>?`, `ATD`/`ATDT`/`ATDP`, `ATA`, `ATH`, `AT&F`,
  and `+NAME`/`#NAME` extended commands (with `=?`/`?`/`=<value>` suffixes —
  see "V.25ter extended commands" below), including chained commands
  (`ATE1V0Q0`). Anything else is rejected as invalid (-> `ERROR`), not
  silently ignored.
- **Modem core** (`ModemStateMachine`): owns the ATE/ATV/ATQ profile and a
  10-register S-register bank, executes parsed commands, and tracks
  `ModemState` (CommandMode / Dialing / Answering / Negotiating /
  OnlineData / Disconnecting). `Execute()` never resolves `ATD`/`ATA`
  itself — see "SIP integration (Phase 2)" above for the async model;
  `HandleCallEvent()` is what actually transitions into
  `OnlineData`/back to `CommandMode` once a real call result exists.
- **Call controller** (`ICallController`): two implementations now.
  `NullCallController` is the Phase 1 stub — every `ATD` honestly returns
  `NO CARRIER`, every `ATA` returns `NO ANSWER`, no SIP/RTP involved — used
  for any `ModemInstance` with no `SipServer` configured.
  `PjsipCallController` (src/Lib/Sip) is the real, PJSIP-backed
  implementation added in Phase 2; see "SIP integration (Phase 2)" above
  for its design and what's been verified.
- **Serial I/O** (`WindowsSerialPort`): opens a real or virtual (com0com)
  COM port via Win32, sets 8N1 with no flow control, and does blocking
  byte-at-a-time reads with a timeout. Manually verified end-to-end
  against a com0com virtual pair and a terminal emulator: the full
  command matrix in `docs/ROADMAP.md` (echo, verbose/numeric/quiet
  modes, register round-trip, dial/answer/hangup, invalid commands)
  passed.
- **Dial string parsing**: `ATD` accepts an optional `T` (tone) or `P`
  (pulse) modifier immediately after the `D`, matching what Windows
  modem drivers actually send (`ATDT<number>` by default). The modifier
  is stripped before the number reaches `ICallController::Dial()` — it
  has no behavioral effect over SIP, but a modifier character leaking
  into the dial string was an actual bug caught during manual testing
  (`ATDT5551234` was dialing `"T5551234"` instead of `"5551234"`).
- **V.25ter extended commands**: a real Windows "Standard Modem Types"
  driver was installed against a com0com port (see "Windows modem driver
  installation" below) and its "Query Modem" diagnostic sent
  `AT+FCLASS=?`, `AT+GMM`, `AT#CLS=?`, `AT+GCI?`/`AT+GCI=?` — none of
  which the parser understood at all (`+`/`#` were unrecognized
  characters, so the whole line failed). `AtCommandParser` now parses
  the general `+NAME`/`#NAME[=?|?|=<value>]` grammar, and
  `ModemStateMachine::ExecuteExtendedCommand` answers only the ones we
  have a true answer for: `+FCLASS=?`/`+FCLASS?` -> `0` (we are a
  plain data modem — no fax/voice class support — which is true, not
  aspirational), `+FCLASS=0` -> `OK` (any other class -> honestly
  `ERROR`), `+GMI`/`+GMM`/`+GMR` -> manufacturer/model/revision strings.
  `+GCI` (country of installation) and `#CLS` (vendor-specific
  voice/fax/data class) are **not** modeled and correctly return `ERROR`
  — there is no physical-PSTN country concept in a SIP-backed virtual
  modem, and `#CLS` is a specific chipset vendor extension we have no
  reason to claim. Verified at the wire level: started `VoIPModem.exe`
  on COM5, sent this exact command sequence from a raw serial client on
  the paired COM6, and confirmed the real byte-for-byte responses match
  (`+FCLASS=?`/`+GMM` return real answers; `#CLS=?`/`+GCI?` return
  `ERROR`; the basic commands Windows also diagnosed, like `ATE0Q0V1`
  and `ATI1`/`ATI7`, were already answering correctly before this fix —
  the earlier "COMMAND NOT SUPPORTED" report covered the whole
  diagnostic batch, not a framing bug in the basic commands).
- **Command-mode session** (`CommandLineSession`): buffers bytes into
  lines on CR, performs local echo when ATE is on, supports backspace/DEL
  editing, and frames responses. Response framing uses `\r\n<text>\r\n`
  uniformly, including in numeric (V0) mode, rather than the bare `\r`
  terminator some real modems use in V0 — a deliberate simplification for
  terminal-emulator compatibility, not a bug.
- **Config** (`IniConfig` / `AppConfig`): flat `[Section] key=value` INI
  parser, now with section discovery (`IniConfig::SectionNames()`) so
  `AppConfig` can find a variable number of `[Modem.<name>]` sections —
  see "Multiple modem instances" above. Per-instance keys:
  `ComPort`, `BaudRate`, `Identification`, `AutoAnswerRingCount`,
  `SipServer`, `SipExtension`, `SipPassword`, `SipRealm`; top-level:
  `Logging.Level`/`Logging.File`, `SIP.LocalPort`. RTP/DSP-specific keys
  (codec preferences, etc.) from `PROJECT_SPEC.md` section 5 are **not
  yet read by any code** and are intentionally absent from
  `ModemInstanceConfig` until Phase 3 adds the modules that consume them.
- **Logging** (`Logger` / `TaggedLogger`): `Logger` is leveled
  (Error/Warn/Info/Debug), writes to an optional file, echoes Warn/Error
  to stderr, and is mutex-protected so multiple `ModemInstance` threads
  can share one safely. `TaggedLogger` wraps it to prefix every message
  with an instance name. SIP credentials (`SipPassword`) now exist in
  config — neither `PjsipCallController` nor `AppConfig`/`ModemInstance`
  log the password anywhere; only the extension/server/registration
  result get logged, per CLAUDE.md's "keep credentials out of logs" rule.

## What does not exist yet

- RTP/audio transport (Phase 3) — SIP *signaling* works
  (`PjsipCallController`), but no media is negotiated or carried;
  `PjsipEndpoint::Start()` explicitly sets a null audio device.
- Modem DSP / modulation / demodulation (Phase 4) — a primitive toolkit
  exists (`src/Lib/Dsp`: oscillator, tone generator/detector, V.21-rate
  FSK modulator/demodulator) and a full ITU-T V.8 handshake
  (`V8Message`/`V8CallMenu`/`V8Signal`/`AnsamTone`/`V8Handshake`) is
  implemented and wired into real call audio via `CallAudioBridge` —
  but it's only been verified in a lossless loopback simulation, not a
  real call yet, and there is still no symbol-timing recovery, no
  retry/timeout handling, and no actual QAM/trellis modulation (V.8
  only negotiates *which* modulation to use next; it doesn't implement
  any of them). See "Modem DSP (Phase 4)" above.
- RAS/PPP integration (Phase 5).
- Actual data flow in online mode: `ModemState::OnlineData` is now reached
  (via `HandleCallEvent(Connected)` once a real SIP call connects), but
  nothing carries data through it yet — that's the DSP/modulation work in
  Phase 4, once Phase 3 gives it real RTP audio to modulate onto.

## Known simplifications (not bugs)

- V0 (numeric) result codes are terminated with `\r\n`, not a bare `\r`.
- A command line longer than 256 characters silently drops the overflow
  rather than erroring, matching the spirit of a real modem's fixed
  command buffer without replicating its exact overflow behavior.
- `ATA`/`ATH` do not currently accept trailing digits (e.g. `ATH1`); the
  spec's required subset only calls for the bare forms.
- `RING` is emitted on a fixed 3-second interval (`ModemInstance::kRingInterval`)
  rather than real telco ring cadence (2s on / 4s off) — there's no
  physical ringing signal to time against over SIP, so this is a
  reasonable fixed substitute, not a measured value.
- A second incoming call while one is already active/ringing is rejected
  with SIP 486 Busy Here rather than queued — a modem line only ever
  handles one call at a time, same as a real modem.
