# VoIPModem — Roadmap Status

Tracks progress against the six phases defined in `PROJECT_SPEC.md`
section 6. Update this file whenever a phase's status changes; do not
mark a phase complete until its acceptance criterion has actually been
exercised, not just coded.

## Phase 1 — Serial modem skeleton — **built, partially verified**

Spec acceptance criterion: *the application behaves like a basic
Hayes-compatible modem in command mode.*

Done:
- Solution/project structure with Core/Hayes/Serial/Config/Logging module
  boundaries (`VoIPModemLib`), plus `VoIPModem` (app) and
  `VoIPModem.Tests`.
- INI config loading with Phase 1's actual key set.
- `AT`, `ATI`, `ATZ`, `ATE`, `ATV`, `ATQ`, `ATS<n>=`/`ATS<n>?`, `ATD`,
  `ATA`, `ATH`, `AT&F` parsing and execution, with correct result codes.
- 27 unit tests covering the parser and state machine, all passing.
- Full solution builds clean (0 warnings at `/W4`) with MSVC 14.51
  (toolset `v145`) via `VoIPModem.sln`.
- `VoIPModem.exe` confirmed to open a real COM3 on the development
  machine, log correctly, and enter its read loop without crashing.

Not yet done:
- **Not tested against com0com or any terminal emulator.** com0com is
  not installed in the current development environment. Opening a real
  COM port and unit-testing the parser are both necessary but not
  sufficient evidence for the spec's acceptance criterion — that
  requires a human (or scripted terminal) actually typing `AT`, `ATI`,
  etc. into one side of a com0com pair and reading the responses on the
  other. **Do this before considering Phase 1 complete.**

## Phase 2 — SIP call control — **not started**

No PJSIP dependency, no `ICallController` implementation beyond the
`NullCallController` stub, no registration, no RING generation. `ATD`
and `ATA` currently always return `NO CARRIER`/`NO ANSWER` by design, not
by partial implementation.

## Phase 3 — RTP audio — **not started**

## Phase 4 — Actual modem interoperability — **not started**

No DSP/modulation code exists anywhere in the repository.

## Phase 5 — Windows networking (RAS/PPP) — **not started**

## Phase 6 — Compatibility and optimization — **not started**

---

## Recommended next step

Verify Phase 1 against com0com + a terminal emulator (e.g. PuTTY or
Tera Term) before starting Phase 2. SIP integration is a much larger
module than the serial skeleton, and starting it on an unverified
foundation risks debugging two unverified layers at once.
