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
A real incoming call was also tested live: **RING generation and `ATA`
both confirmed working.** Auto-answer did not, and the cause was a real
bug, not just an unverified path: `ModemInstanceConfig::autoAnswerRingCount`
was parsed from `AutoAnswerRingCount=` but never written into the S0
register anywhere, so it silently had no effect unless `ATS0=n` was
typed manually first. Fixed via `ModemStateMachine::InitializeAutoAnswerRegister()`,
called from `ModemInstance`'s constructor — **re-verify auto-answer
specifically** against a real incoming call before treating it as done.
Also not yet verified: the graceful Ctrl+C shutdown path now that it has
to tear down `PjsipCallController`/`PjsipEndpoint` too (only
forced-process-kill shutdown has been exercised with SIP active so far).

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
- Modem DSP / modulation / demodulation (Phase 4) — no audio or bitstream
  processing exists anywhere in the codebase.
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
