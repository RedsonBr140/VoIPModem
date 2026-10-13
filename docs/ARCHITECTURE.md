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
    Config/                INI loading, AppConfig
    Logging/                Leveled logger
  App/                     VoIPModem (executable)
    Application.*          Wires config/logging/serial/core together, runs the read loop
    CommandLineSession.*    Byte-level line buffering, local echo, response framing
    main.cpp               Entry point, Ctrl+C handling
tests/                     VoIPModem.Tests (executable)
  TestFramework.h          Hand-rolled registration/assert macros (no external dependency)
  AtCommandParserTests.cpp
  ModemStateMachineTests.cpp
  FakeCallController.h    Deterministic ICallController test double
```

## Dependency direction

```
main.cpp -> Application -> CommandLineSession -> ModemStateMachine (Core)
                        \-> WindowsSerialPort (Serial)      -> AtCommandParser (Hayes)
                        \-> NullCallController (Core)
```

`ModemStateMachine` depends only on `ICallController`, never on
`WindowsSerialPort` or any SIP type. This is deliberate: it is the seam
where Phase 2's PJSIP-backed call controller plugs in without the state
machine changing. `ISerialPort` exists for the same reason on the serial
side, though nothing currently implements it besides `WindowsSerialPort`.

## What exists today (Phase 1)

- **Hayes parser** (`AtCommandParser`): parses `AT`, `ATI`, `ATZ`, `ATE`,
  `ATV`, `ATQ`, `ATS<n>=<v>`, `ATS<n>?`, `ATD`, `ATA`, `ATH`, `AT&F`,
  including chained commands (`ATE1V0Q0`). Anything else is rejected as
  invalid (-> `ERROR`), not silently ignored.
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
  there is a telephony backend. **There is no SIP/RTP integration.**
- **Serial I/O** (`WindowsSerialPort`): opens a real or virtual (com0com)
  COM port via Win32, sets 8N1 with no flow control, and does blocking
  byte-at-a-time reads with a timeout. Verified to open a real COM3 on
  the development machine and enter the listen loop without crashing;
  not yet verified against a com0com virtual pair or an actual terminal
  emulator typing AT commands end-to-end.
- **Command-mode session** (`CommandLineSession`): buffers bytes into
  lines on CR, performs local echo when ATE is on, supports backspace/DEL
  editing, and frames responses. Response framing uses `\r\n<text>\r\n`
  uniformly, including in numeric (V0) mode, rather than the bare `\r`
  terminator some real modems use in V0 — a deliberate simplification for
  terminal-emulator compatibility, not a bug.
- **Config** (`IniConfig` / `AppConfig`): flat `[Section] key=value` INI
  parser. Only reads the keys Phase 1 actually uses (`ComPort`, `BaudRate`,
  `Identification`, `AutoAnswerRingCount`, `Logging.Level`, `Logging.File`).
  SIP/RTP/DSP keys from `PROJECT_SPEC.md` section 5 are **not yet read by
  any code** and are intentionally absent from `AppConfig` until Phase 2+
  adds the modules that consume them.
- **Logging** (`Logger`): leveled (Error/Warn/Info/Debug), writes to an
  optional file and echoes Warn/Error to stderr. No credentials exist in
  the system yet, so there is nothing to accidentally log — this will
  matter once SIP auth is added in Phase 2.

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
