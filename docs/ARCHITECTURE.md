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
    Config/                INI loading, AppConfig (one ModemInstanceConfig per line)
    Logging/               Leveled logger + TaggedLogger (per-instance prefix)
  App/                     VoIPModem (executable)
    Application.*          Loads config, owns the shared Logger, starts/stops all ModemInstances
    ModemInstance.*        One modem line: its own port/controller/core/session, own thread
    CommandLineSession.*    Byte-level line buffering, local echo, response framing
    main.cpp               Entry point, Ctrl+C handling
tests/                     VoIPModem.Tests (executable)
  TestFramework.h          Hand-rolled registration/assert macros (no external dependency)
  AppConfigTests.cpp
  AtCommandParserTests.cpp
  ModemStateMachineTests.cpp
  FakeCallController.h    Deterministic ICallController test double
```

## Dependency direction

```
main.cpp -> Application -> ModemInstance (one per modem line) -> CommandLineSession -> ModemStateMachine (Core)
                                                               \-> WindowsSerialPort (Serial) -> AtCommandParser (Hayes)
                                                               \-> NullCallController (Core)
```

`ModemStateMachine` depends only on `ICallController`, never on
`WindowsSerialPort` or any SIP type. This is deliberate: it is the seam
where Phase 2's PJSIP-backed call controller plugs in without the state
machine changing. `ISerialPort` exists for the same reason on the serial
side, though nothing currently implements it besides `WindowsSerialPort`.

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

## What exists today (Phase 1)

- **Hayes parser** (`AtCommandParser`): parses `AT`, `ATI`, `ATZ`, `ATE`,
  `ATV`, `ATQ`, `ATS<n>=<v>`, `ATS<n>?`, `ATD`/`ATDT`/`ATDP`, `ATA`, `ATH`, `AT&F`,
  and `+NAME`/`#NAME` extended commands (with `=?`/`?`/`=<value>` suffixes —
  see "V.25ter extended commands" below), including chained commands
  (`ATE1V0Q0`). Anything else is rejected as invalid (-> `ERROR`), not
  silently ignored.
- **Modem core** (`ModemStateMachine`): owns the ATE/ATV/ATQ profile and a
  10-register S-register bank, executes parsed commands, and tracks
  `ModemState` (CommandMode / Dialing / Answering / Negotiating /
  OnlineData / Disconnecting). Only CommandMode is actually reachable
  today; Dialing/Answering are entered and exited within a single
  `Execute()` call because the only `ICallController` implementation is a
  stub.
- **Call controller stub** (`NullCallController`): every `ATD` returns
  `NO CARRIER`, every `ATA` returns `NO ANSWER`. It exists so the dial/answer
  code paths in the state machine are real and tested, without pretending
  there is a telephony backend. **There is no SIP/RTP integration.** Each
  `ModemInstance` currently gets its own `NullCallController`; Phase 2 is
  expected to give each instance its own SIP-backed `ICallController`
  instead, tied to that instance's own extension.
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
  see "Multiple modem instances" above. Only reads the keys Phase 1
  actually uses per instance (`ComPort`, `BaudRate`, `Identification`,
  `AutoAnswerRingCount`) plus top-level `Logging.Level`/`Logging.File`.
  SIP/RTP/DSP keys from `PROJECT_SPEC.md` section 5 are **not yet read by
  any code** and are intentionally absent from `ModemInstanceConfig`
  until Phase 2+ adds the modules that consume them.
- **Logging** (`Logger` / `TaggedLogger`): `Logger` is leveled
  (Error/Warn/Info/Debug), writes to an optional file, echoes Warn/Error
  to stderr, and is mutex-protected so multiple `ModemInstance` threads
  can share one safely. `TaggedLogger` wraps it to prefix every message
  with an instance name. No credentials exist in the system yet, so
  there is nothing to accidentally log — this will matter once SIP auth
  is added in Phase 2.

## What does not exist yet

- SIP/RTP transport (Phase 2/3) — `NullCallController` is the only
  `ICallController`.
- Modem DSP / modulation / demodulation (Phase 4) — no audio or bitstream
  processing exists anywhere in the codebase.
- RAS/PPP integration (Phase 5).
- Online data mode: `ModemState::OnlineData` is defined but nothing ever
  transitions into it, because nothing can currently negotiate a
  connection to go online with.
- Auto-answer on an incoming call: `ATS0=n`/`ATS0?` read and write the
  register correctly, but nothing triggers auto-answer, because there is
  no SIP layer to generate a RING event in the first place.

## Known simplifications (not bugs)

- V0 (numeric) result codes are terminated with `\r\n`, not a bare `\r`.
- A command line longer than 256 characters silently drops the overflow
  rather than erroring, matching the spirit of a real modem's fixed
  command buffer without replicating its exact overflow behavior.
- `ATA`/`ATH` do not currently accept trailing digits (e.g. `ATH1`); the
  spec's required subset only calls for the bare forms.
