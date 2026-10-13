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

## Phase 2 — SIP call control — **complete, fully verified**

Spec acceptance criterion: *incoming and outgoing SIP calls work
reliably, with correct serial responses.*

Done:
- PJSIP (pjproject 2.17) vendored as a git submodule and built from
  source (no vcpkg/Conan package exists for it) — see
  `docs/ARCHITECTURE.md` "SIP integration (Phase 2)" for the full build
  story, including the `libpjproject` vs. `pjsua2_lib` target gotcha.
- `PjsipCallController` (src/Lib/Sip), a second `ICallController`
  implementation alongside `NullCallController`. `ModemStateMachine`
  did **not** need to change its shape to add this — it only needed to
  stop pretending `ATD`/`ATA` resolve synchronously, which was always a
  Phase-1-stub-only simplification (see the async model in
  `docs/ARCHITECTURE.md`).
- Per-instance SIP registration (`SipServer`/`SipExtension`/`SipPassword`/`SipRealm`
  config keys), one process-wide `PjsipEndpoint`/UDP transport
  (`[SIP] LocalPort`) shared by all instances' accounts.
- RING cadence + S0 auto-answer, `ATD`/`ATA`/`ATH` wired to real SIP
  INVITE/answer/BYE, SIP status code -> Hayes result code mapping.
- 55 unit tests (17 new), all passing.
- **Verified against a real Asterisk test server** (10.174.7.172:5060,
  extension 1003): SIP registration succeeded (200 OK), and `ATD` to an
  unroutable number went through the real async SIP signaling round-trip
  and correctly produced `NO CARRIER` on the actual serial port — not a
  mock, not just a unit test.

Then live-tested by the user against a real incoming call: **RING and
`ATA` both confirmed working.** Auto-answer initially did not — root
cause was a real bug, not just an untested path:
`ModemInstanceConfig::autoAnswerRingCount` (`AutoAnswerRingCount=` in the
INI) was parsed but never actually written into the S0 register, so it
silently did nothing unless `ATS0=n` was typed manually first. Fixed via
`ModemStateMachine::InitializeAutoAnswerRegister()`, called once from
`ModemInstance`'s constructor, with a regression test
(`InitializeAutoAnswerRegister_SeedsS0FromConfig`).

**Re-tested live after the fix**: auto-answer confirmed working, and a
second instance was registered (extension 1004, same password, same
Asterisk server) — both instances reached `CONNECT` on real calls
simultaneously. Phase 2's acceptance criterion ("incoming and outgoing
SIP calls work reliably, with correct serial responses") is met.

Still not verified, but not blocking (pre-existing gaps, not Phase-2
regressions): graceful Ctrl+C shutdown with a
`PjsipCallController`/`PjsipEndpoint` actually torn down (only
forced-process-kill has been exercised with SIP active).

## Phase 3 — RTP audio — **complete, verified live**

Spec acceptance criterion: *bidirectional modem-like audio can pass
through the VoIP path without the application corrupting it.*

Done:
- Media config tuned for modem-tone preservation per
  `docs/PROJECT_SPEC.md` section 4.D: native 8kHz clock rate (no
  resampling), VAD/silence-suppression disabled, echo cancellation
  disabled. See `docs/ARCHITECTURE.md` "RTP audio (Phase 3)" for exactly
  which `MediaConfig` fields and why — and the one gap, that pjsua2
  exposes no explicit AGC toggle to disable.
- Codec negotiation restricted process-wide to G.711 (PCMU/PCMA) only;
  every other codec (Opus, Speex, GSM, iLBC, ...) is disabled outright,
  not just de-prioritized, per section 4.E.
- First audio sink tried: each call's active audio connected to this
  machine's default sound device on `onCallMediaState`, chosen over a
  stubbed DSP-facing port after discussing the tradeoff with the user —
  a real, byear-verifiable proof that G.711/RTP/codec fidelity are
  correct, without blocking Phase 4. **Live-tested by the user
  immediately — failed**: `PJMEDIA_EAUD_NODEFDEV` on both playback and
  capture. Traced through pjproject's own source to the real cause: this
  dev machine has zero capture devices at the Win32 API level
  (`waveInGetNumDevs() == 0`, confirmed directly via a `winmm.dll`
  P/Invoke check), and pjsua2's entire audio-device layer — both the
  default-device lookup and the `ExtraAudioDevice` alternative —
  hardcodes duplex (`PJMEDIA_DIR_CAPTURE_PLAYBACK`); there is no
  playback-only mode anywhere in it. A real PJSIP/hardware constraint on
  this machine, not a VoIPModem bug — confirmed with the user (no mic on
  this PC, as expected), who then chose the alternative below over
  testing on different hardware instead.
- **Current audio sink**: an `AudioMediaRecorder` (incoming call audio ->
  `call-recording-<extension>-<timestamp>.wav`) and an
  `AudioMediaPlayer` (loops a bundled `assets/test-tone.wav` — an 8kHz
  mono 16-bit 1kHz sine tone — into the call). Neither needs any
  physical sound device, so it's independent of this machine's hardware
  gap. `test-tone.wav` is copied next to `VoIPModem.exe` by a
  `VoIPModem.vcxproj` post-build step; both file paths resolve relative
  to the running `.exe`, not the working directory.
- Smoke-tested: both instances start, register, and open with zero
  device/media errors (expected — nothing touches a device anymore).

**Verified live** (2026-10-13): a physical analog line, through the
Cisco FXS gateway and Asterisk, dialed into extension 1003 — the call
reached active media, `call-recording-1003-*.wav` came out clean (not
silent, not corrupted), and the test tone was clearly audible on the
analog line's end. Phase 3's acceptance criterion ("bidirectional
modem-like audio can pass through the VoIP path without the application
corrupting it") is met for a sine tone / human-voice-shaped signal.

Carried into Phase 4, not yet proven: this only shows a sine tone (or
human voice) survives the path — not that an actual modem handshake
tone would survive undistorted at the bit level. That distinction is
Phase 4's to prove, once a DSP engine exists that can tell "sounds
fine" from "decodes correctly" apart.

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

Phases 1-3 are done and verified live. Start Phase 4 (actual modem
interoperability): pick and integrate a DSP/modulation engine (prefer an
established library over writing a protocol stack from scratch, per
CLAUDE.md), starting with the modest target `PROJECT_SPEC.md` section
4.E specifies — V.32bis at 9600 bit/s — before attempting anything
faster. The WAV-based `AudioMediaRecorder`/`AudioMediaPlayer` sink in
`PjsipCallController` is exactly the seam this plugs into: Phase 4
replaces that sink with the DSP engine's PCM frames in and out, without
needing to touch SIP signaling or the modem state machine at all.
