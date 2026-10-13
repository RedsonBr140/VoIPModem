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
- 38 unit tests covering the parser, state machine, and config loading,
  all passing.
- **Multiple concurrent modem instances**, each with its own COM port
  (and, from Phase 2 on, its own SIP extension): see
  `docs/ARCHITECTURE.md` "Multiple modem instances". Added ahead of
  Phase 2 specifically so SIP integration starts with the per-instance
  boundary already in place, rather than retrofitting it later. A
  two-instance config was smoke-tested (both COM ports opened and ran
  concurrently without crashing), but **not yet verified with a terminal
  emulator exchanging AT commands against two instances at once**, and
  the graceful-shutdown path hasn't been exercised with N > 1 instances
  via an actual Ctrl+C (only via a forced process kill so far). Do this
  before relying on it for Phase 2 testing.
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

- Fixed a second real gap found via a real Windows "Standard Modem"
  driver's diagnostics (see below): `AT+FCLASS=?`, `AT+GMM`, `AT#CLS=?`,
  `AT+GCI?` all failed outright because the parser didn't understand
  `+`/`#` extended commands at all. Added general extended-command
  parsing and honest answers for `+FCLASS`/`+GMI`/`+GMM`/`+GMR`; `+GCI`
  and `#CLS` correctly still return `ERROR` since neither is modeled.
  Verified at the wire level against the real running app (see
  `docs/ARCHITECTURE.md` "V.25ter extended commands").
- **Windows now recognizes a com0com port as an actual modem device**:
  installed via Device Manager's "Add legacy hardware" wizard using the
  generic "(Standard Modem Types)" driver, bound to the COM port on the
  *opposite* side of the com0com pair from the one VoIPModem itself
  opens — e.g. VoIPModem's `[Modem.Line1]` listens on COM3, so the
  Windows modem driver goes on COM4 (confirmed pairing via `com0com`'s
  own `setupc.exe list`, since the two sides aren't always adjacent COM
  numbers: this machine's pairs are COM3↔COM4 and COM5↔COM6). Getting
  this backwards (driver on the same port VoIPModem has open) doesn't
  work — that port is already held open by VoIPModem.

## Phase 2 — SIP call control — **not started**

No PJSIP dependency, no `ICallController` implementation beyond the
`NullCallController` stub, no registration, no RING generation. `ATD`
and `ATA` currently always return `NO CARRIER`/`NO ANSWER` by design, not
by partial implementation.

## Phase 3 — RTP audio — **not started**

## Phase 4 — Actual modem interoperability — **not started**

No DSP/modulation code exists anywhere in the repository.

## Phase 5 — Windows networking (RAS/PPP) — **not started**

One prerequisite is done early: Windows RAS dials through a *modem
device*, not a bare COM port, so at least one com0com port pair now has
a real "(Standard Modem Types)" driver bound to it (see Phase 1's
"Windows now recognizes..." entry above for exactly which port and why).
RAS integration itself — authentication, address assignment, routing,
DNS, and an actual dial-up networking session from the Windows 98
client — has not been started.

## Phase 6 — Compatibility and optimization — **not started**

---

## Recommended next step

Phase 1 is verified. Begin Phase 2 (SIP call control via PJSIP) by
adding a new `ICallController` implementation alongside
`NullCallController` — `ModemStateMachine` should not need to change,
since that interface is the seam it was designed around.
