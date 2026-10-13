# VoIPModem — Roadmap Status

Tracks progress against the six phases defined in `PROJECT_SPEC.md`
section 6. Update this file whenever a phase's status changes; do not
mark a phase complete until its acceptance criterion has actually been
exercised, not just coded.

## Phase 1 — Serial modem skeleton — **complete**

Spec acceptance criterion: *the application behaves like a basic
Hayes-compatible modem in command mode.*

Done:
- Solution/project structure with Core/Hayes/Serial/Config/Logging module
  boundaries (`VoIPModemLib`), plus `VoIPModem` (app) and
  `VoIPModem.Tests`.
- INI config loading with Phase 1's actual key set.
- `AT`, `ATI`, `ATZ`, `ATE`, `ATV`, `ATQ`, `ATS<n>=`/`ATS<n>?`,
  `ATD`/`ATDT`/`ATDP`, `ATA`, `ATH`, `AT&F` parsing and execution, with
  correct result codes.
- 33 unit tests covering the parser and state machine, all passing.
- Full solution builds clean (0 warnings at `/W4`) with MSVC 14.51
  (toolset `v145`) via `VoIPModem.sln`.
- **Manually verified end-to-end against a com0com virtual pair and a
  terminal emulator**: echo on/off, verbose/numeric/quiet result codes,
  register set/query round-trip, `ATD`/`ATDT`/`ATDP`/`ATA`/`ATH`,
  chained commands, invalid-command `ERROR`, and clean Ctrl+C shutdown
  all confirmed against real serial I/O, not just unit tests.
- Fixed a real bug found during that manual pass: `ATDT<number>` was
  dialing the literal string `"T<number>"` instead of stripping the tone
  modifier. This mattered because Windows modem drivers send `ATDT` by
  default, so it would have broken RAS dial-out before Phase 5 even
  started. See `docs/ARCHITECTURE.md` "Dial string parsing".

Known non-issue, not a Phase 1 gap: a bare com0com COM port is not
automatically recognized by Windows as a "modem" — that's expected, and
is addressed under Phase 5 below, not here.

## Phase 2 — SIP call control — **not started**

No PJSIP dependency, no `ICallController` implementation beyond the
`NullCallController` stub, no registration, no RING generation. `ATD`
and `ATA` currently always return `NO CARRIER`/`NO ANSWER` by design, not
by partial implementation.

## Phase 3 — RTP audio — **not started**

## Phase 4 — Actual modem interoperability — **not started**

No DSP/modulation code exists anywhere in the repository.

## Phase 5 — Windows networking (RAS/PPP) — **not started**

Prerequisite not yet done, and not a code change: Windows RAS dials
through a *modem device*, not a bare COM port. A com0com virtual port
has to be bound to a generic Hayes-compatible modem driver before RAS
can use it — e.g. Device Manager → Add Legacy Hardware → Modem →
"Standard Modem Types" → a generic Hayes-compatible entry (or
"Communications cable between two computers"), attached to the
com0com port VoIPModem listens on. The AT command set implemented in
Phase 1 is exactly what that generic driver expects to speak, so no
VoIPModem code is implicated — this is deployment/configuration to do
once Phase 5 actually starts RAS integration work.

## Phase 6 — Compatibility and optimization — **not started**

---

## Recommended next step

Phase 1 is verified. Begin Phase 2 (SIP call control via PJSIP) by
adding a new `ICallController` implementation alongside
`NullCallController` — `ModemStateMachine` should not need to change,
since that interface is the seam it was designed around.
