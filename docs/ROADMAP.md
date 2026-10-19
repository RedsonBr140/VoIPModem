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

**Update**: this WAV-based sink has since been replaced by
`CallAudioBridge` (Phase 4's first wiring increment — see below). The
description above is a historical record of what Phase 3 verified live
at the time, not current behavior.

## Phase 4 — Actual modem interoperability — **in progress**

**Library survey (2026-10-17), before writing any DSP code:** per
CLAUDE.md's "prefer established protocol libraries over writing complex
protocol stacks from scratch," real candidates were checked against their
actual source, not assumed:
- **spandsp** (LGPL 2.1) has real, working V.22bis (2400 bps) but its own
  `v32bis.c` is a ~15%-done facade over V.17 with no handshake/negotiation
  implemented, and its `v34rx.c`/`v34tx.c` are a genuine 25-60% done but
  explicitly self-marked `NOT YET FUNCTIONAL!` (disabled equalizer/carrier
  tracking/Viterbi decode on rx; hardcoded/empty training-state branches on
  tx). It was briefly vendored as a submodule to target V.22bis first, but
  the user decided that path was "too big, too old, too coupled" (its
  fax-oriented, TIFF-dependent, monolithic `data_modems.c` dispatcher)
  relative to building V.34 directly — **spandsp vendoring was reverted**
  before any of it was wired up.
- **slmodemd**, via `AonCyberLabs`/`strozfriedberg` D-Modem, is the one
  implementation found that actually achieves real V.32bis/V.34 data
  connections over SIP — but it's Linux/x86-32-only, GPL-2 with a murky,
  more-restrictive license past v2.9.9, and its own README states
  receiving calls is **not supported**, directly conflicting with this
  project's mandatory incoming-call requirement. Not vendored. Used only
  as an architectural reference (how the problem is structured), never as
  a code source — see the licensing note below.
- **fisher-modem** (MIT) has no V.32bis at all, and its own author says
  V.32 "never seems to read the rates it's expecting."
- Microchip's dsPIC30F softmodem library ships V.32bis as object code
  only (no source); VOCAL's modem stack is commercial, not open source.

**Decision**: build the V.34 DSP/handshake engine from scratch in
`src/Lib/Dsp` (a new module boundary, kept as separate from `src/Lib/Sip`
as `ICallController` keeps modem logic separate from serial I/O — see
`docs/ARCHITECTURE.md` "Modem DSP (Phase 4)"). This is a deliberate
departure from `PROJECT_SPEC.md` section 4.E's literal example target
(V.32bis at 9600 bps as the first milestone) — the user chose to aim
directly at V.34/28,800 bps instead, accepting that this is a
substantially harder DSP problem (line probing, shell mapping, trellis
coding, adaptive equalizer, same-band full-duplex echo cancellation) than
the spec's own suggested starting point. **Licensing note**: the actual
ITU-T V.34 and V.8 Recommendations are the implementation reference, not
slmodemd's source — slmodemd/D-Modem's GPL-2 (and slmodemd's own murkier
licensing past v2.9.9) means its code must not be ported or transcribed
into VoIPModem, which isn't GPL.

Done so far:
- `src/Lib/Dsp` module skeleton: `Dds` (phase-accumulator oscillator,
  shared by tone generation now and QAM carrier modulation later),
  `ToneGenerator` (TX-side pure tones, e.g. for the V.25/V.8 handshake),
  `GoertzelDetector` (single-bin DFT tone-energy detector, the RX-side
  counterpart). All operate at 8 kHz, matching the sample rate
  `PjsipEndpoint` already negotiates with no resampling (Phase 3).
- 6 unit tests (63 total, all passing): exact-value checks on `Dds`'s
  phase accumulator (a quarter-sample-rate tone has an exactly verifiable
  sin/cos sequence, not just a floating-tolerance check), and
  `GoertzelDetector` correctly reading ~1.0 magnitude for a tone at its
  target frequency, ~0 for a tone elsewhere or for silence, and clearing
  correctly on `Reset()`.

**Call-media wiring, done next (same session)**: per
`docs/ARCHITECTURE.md` "Modem DSP (Phase 4)" for full detail —
`PjsipCallController::SipCall::onCallMediaState` now creates a
`CallAudioBridge` (`src/Lib/Sip`) instead of Phase 3's WAV
recorder/player. It's a custom pjsua2 `AudioMediaPort`: the answering
side generates the real ITU-T V.25 answer tone (2100 Hz) into the call,
and both sides run a `GoertzelDetector` over incoming audio and log tone
detections at a placeholder target frequency. `callingParty` is threaded
from `Dial()`/`onIncomingCall()` into `SipCall` so the bridge knows its
role. Build-verified clean (`VoIPModem.sln`, 0 warnings at `/W4`) and all
63 unit tests pass. This is explicitly a seam-verification step, not a
V.8/V.25 handshake — no negotiation, no state machine yet.

**Live-tested 2026-10-17**: a real call through the Cisco FXS gateway
into extension 1004 (`[Modem.Line2]`; `[Modem.Line1]`/COM4 failed to
open — pre-existing Access Denied, unrelated to this change, not yet
resolved). Auto-answered after 2 rings, ran ~24s, hung up — the log
showed only the `ATA`/`ATH` lines either side, nothing confirming media
went active, because `CallAudioBridge::Start()` had no success log of
its own yet. The actual confirmation was a human on the physical line
hearing "a dial-up tone, that static" during the call — real proof the
generated tone survived the full SIP/RTP/G.711/FXS path. Fixed the gap
this exposed: `CallAudioBridge::Start()` now logs its role/TX/RX
configuration on success, so the next test doesn't depend solely on
someone's ear. See `docs/ARCHITECTURE.md` "Modem DSP (Phase 4)" for the
full account.

**FSK primitive added (same session)**: `FskModulator`/`FskDemodulator`
(`src/Lib/Dsp`) — continuous-phase binary FSK at a configurable
mark/space/baud rate, confirmed against V.21's own frequency table
(channel 1: mark 980 Hz/space 1180 Hz; channel 2: mark 1650 Hz/space
1850 Hz; 300 baud) rather than assumed. Includes V.21/V.8-style async
byte framing (1 start/8 data LSB-first/1 stop, no parity). 5 new unit
tests (68 total): bit-level loopback, byte-level async framing loopback,
two misalignment/malformed-input cases, and — importantly —
`Fsk_ChunkedDelivery_MatchesWholeBufferDecoding`, which proves the exact
pattern real call audio needs (samples arriving in small 160-sample/20ms
frames, not one buffer) decodes identically to one-shot decoding, before
any of this touched real call audio.

**`CallAudioBridge` now uses FSK instead of a single tone**: superseding
the first increment's tone-only probe (still described above for
history), each side now continuously transmits an incrementing counter
byte via `FskModulator` on its role-correct V.21 channel (calling party:
channel 1 TX / channel 2 RX; answering party: channel 2 TX / channel 1
RX) and continuously tries to decode the other side's bytes back out via
`FskDemodulator`, logging each decoded byte and flagging if it's not
sequential (a real decode error, not just "no signal"). Build-verified
clean (`VoIPModem.sln`, 0 warnings at `/W4`) and all 68 tests pass.
Live-tested (2026-10-17) — see the incremental-demodulator entry below
for what that confirmed and what it didn't.

**`FskDemodulator` made properly incremental (same session, after a live
bug)**: the original RX design re-decoded the *entire* accumulated call
audio from scratch every 20ms frame — proven correct in isolation
(`Fsk_ChunkedDelivery_MatchesWholeBufferDecoding` passed), but once
wired into a real call this was confirmed live (2026-10-17) to grow
per-frame CPU work without bound, and after roughly 90 seconds of real
call time no bytes ever decoded and the call stopped responding to
teardown. Fixed by rewriting `FskDemodulator::ProcessSamples` to carry
all bit-boundary state across calls (O(1) amortized per sample, no
growing buffer); `CallAudioBridge` now keeps a periodically-compacted
`pendingBits_` instead. Re-tested live the same day: a call answered by
`CallAudioBridge` stayed healthy for 3+ minutes (near-zero CPU, flat
memory) and shut down cleanly on `ATH`/Ctrl+C — the runaway is fixed.
Zero `decoded byte` log lines appeared in that test, but that's
expected: the far end was a plain phone, which has no FSK transmitter
of its own, so there was nothing of ours to decode on the other side.
No symbol-timing recovery exists either: both sides' bit boundaries are
only correct because they each start counting from their own call
start, not because of any real synchronization derived from the signal
itself.

**Known bug, confirmed live, deliberately deferred (2026-10-17)**:
hanging up from the far end (a physical phone, not `ATH`) does not tear
down the call on our side — the exact symptom originally reported.
This was re-confirmed *after* the CPU fix above, which rules out the
original "overloaded media thread delays BYE processing" theory: CPU
time stayed near zero (0.375s total) and memory flat (~16.5MB) while
the call sat stuck, so this is not a busy-loop. No new log line
appears at all after `CallAudioBridge started` once the far end hangs
up — PJSIP's `onCallState` never fires `PJSIP_INV_STATE_DISCONNECTED`,
which most likely means no SIP BYE ever arrives from Asterisk in the
first place (an FXS disconnect-supervision issue on the Cisco
gateway/Asterisk side, not necessarily a VoIPModem bug) — but this is
unconfirmed. An attempt to check Asterisk's `core show channels
verbose` for the stuck call was inconclusive because VoIPModem.exe was
stopped before the check ran, which tears down our own SIP dialog on
exit and makes Asterisk's "0 channels" result meaningless as evidence.
Correctly diagnosing this needs: dial in, hang up from the phone, and
check Asterisk's channel state *while VoIPModem.exe is still running
and stuck* — not done yet. Per explicit user direction, this is
logged here to come back to, not fixed now; Phase 4 DSP work continues
in the meantime.

**V.8 octet-level codec added (`V8Message`, same session)**: the actual
ITU-T Recommendation V.8 (11/2000) text was obtained (its PDF, publicly
downloadable from itu.int, converted to text via `pdftotext`) and read
in full — not assumed from secondary sources. `src/Lib/Dsp/V8Message.h`/
`.cpp` implements the bit-level coding format from clauses 5 and 6:
generic category/extension octet packing (5.1/5.2), the call function
octet (Table 3, including the `Data` function this project actually
needs), the 3-octet modulation modes field (Table 4, covering V.21
through V.34), PCM modem availability (Table 5), the protocol octet
(Table 6, LAPM), the PSTN access octet (Table 7), the CI/CM/JM preamble
bit patterns (Table 1), and the 3-zero-octet CJ terminator (3.5). Each
bit mapping was checked against a clean raw-text re-extraction of the
PDF's tables (the first layout-preserving extraction garbled some
columns) before being coded, specifically to avoid guessing. 15 new
unit tests (83 total), each asserting an exact bit pattern or round-
trip against the spec's own tables, e.g. `V8_CallFunction_DataRoundTrips`
checks the `Data` octet is literally `b5=0,b6=1,b7=1` as Table 3 states.
Build-verified clean (0 warnings at `/W4`); no DSP or audio involved at
all yet — this is pure bit packing/unpacking, the same "primitive in
isolation first" discipline as `Dds`/`GoertzelDetector` before
`FskModulator`/`FskDemodulator` existed.

**The full V.8 handshake implemented, loopback-tested, and wired into
real call audio (same session)**: built on the octet codec above, in
order:
- `V8CallMenu` (`src/Lib/Dsp`): bundles call function + modulation
  modes + optional protocol/PSTN access/PCM into one menu, with
  `EncodeCallMenu`/`DecodeCallMenu`, `IntersectModulationModes` (what
  JM's modes should be per 7.4 when PCM isn't involved), and
  `SelectModulationMode` (7.4's "lowest item number wins" rule, Table 4
  items 1-12).
- `V8Signal`: builds one CI/CM/JM/CJ bit sequence (preamble + octets)
  through an `FskModulator`; `FindBitPattern` to locate a preamble in a
  demodulated bitstream; `ReadCallMenuAfterPreamble` to greedily decode
  octets after one; `LooksLikeCj` for the no-preamble 3-zero-octet CJ
  signal.
- `AnsamTone`: a new DSP primitive, `AnsamGenerator`/`AnsamDetector` —
  2100 Hz with 15 Hz AM (envelope 0.8x-1.2x) and 450ms phase reversals
  (7.2), the phase reversal implemented as just negating the sample
  (`sin(x+pi) == -sin(x)`).
- `V8Handshake`: the actual clause 8 state machine for both roles (call
  DCE: wait for ANSam, silent Te, transmit CM while waiting for 2
  identical JM, send CJ; answer DCE: initial silence, transmit ANSam
  while waiting for 2 identical CM, transmit JM while waiting for CJ).
  CI is deliberately skipped (7.1: optional). Honestly simplified: Te
  and the answer DCE's initial silence are fixed at the spec's own
  minimums, there's no retry/timeout handling at all (a stuck
  handshake just sits in `InProgress` forever), and JM's call function
  never differs from what's locally offered (8.2.3's fallback path
  isn't modeled).

**Two real bugs found via this class's own loopback unit test (not
guessed at, not caught by code review) and fixed**:
1. The receiving `FskDemodulator`s started ticking their bit-clock as
   soon as each state began — well before the other side's real
   signal actually arrived (e.g. the answer DCE's `cmDemodulator_` had
   been running for ~1 second before real CM ever showed up, during
   ANSam/Te). Since this codebase has no real symbol-timing recovery,
   that left the receiver's bit-slicing windows phase-misaligned with
   the transmitter's actual bit edges, and CM/JM were never recognized
   at all despite being transmitted correctly. Fixed with a pragmatic
   (not real clock recovery) edge-sync: `cmDemodulator_`/
   `jmDemodulator_` became `std::optional<FskDemodulator>`, lazily
   constructed the moment energy specifically at that channel's own
   mark/space frequencies is first seen (a plain amplitude check was
   tried first and failed: it synced to ANSam's 2100 Hz tone, still
   playing on the shared audio, instead of real JM).
2. Once sync was fixed, every preamble was still found but every menu
   decode failed. Cause: the preamble search kept finding the *same*
   real CM repetition a few bits too early (before its trailing octets
   had fully arrived), and treated "not enough data yet" the same as
   "genuine false positive," permanently skipping past a real preamble
   instead of retrying it once more data existed. Fixed by making
   `ReadCallMenuAfterPreamble` only commit to decoding once it hits a
   *genuine* framing failure (the real end-of-sequence marker — the
   next repetition's preamble starts with mark/1 bits, which can never
   be a valid start bit) rather than merely running out of buffered
   bits, and having the caller always retry from the same preamble
   position on failure rather than skip forward.

**Result**: `V8Handshake_FullLoopback_BothSidesNegotiate` and related
tests (8 new, 105 total) now reach `Negotiated` on both sides end to
end — ANSam detected, Te observed, CM/JM matched twice each, CJ sent
and received — including correctly intersecting mismatched modulation
lists (`V8Handshake_MismatchedModes_JmReflectsOnlyTheIntersection`:
answer DCE lacking V.34 correctly yields a V.21-only JM, not a false
V.34 claim). This is a **lossless, in-process loopback simulation**
(each side's TX fed directly as the other's RX, no real audio path,
no noise) — it proves the state machine itself is internally
consistent, not that it survives a real SIP/RTP/G.711 call.

**Wired into real call audio (`CallAudioBridge`, same session,
superseding the FSK counter-byte probe)**: `CallAudioBridge` now runs
one `V8Handshake` per call (`CallDce` for the calling party, `AnswerDce`
for the answering party), advertising **only V.21** in its local menu —
deliberately not V.32bis/V.34/anything else, since V.21 FSK is the only
modulation this codebase actually has working, per CLAUDE.md's rule
against claiming a standard before it's implemented. Logs the
negotiated mode once `Negotiated` is reached. Because pjsua2 calls
`onFrameReceived`/`onFrameRequested` as separate callbacks but
`V8Handshake::ProcessFrame` wants both at once, the real work happens in
`onFrameReceived` (computing that frame's TX output as a side effect
into `pendingTxSamples_`), and `onFrameRequested` just drains that
buffer (silence on a possible first-frame ordering gap). Build-verified
clean (`VoIPModem.sln`, 0 warnings at `/W4`); **not yet live-tested**.
Critically, a live test of this needs **two VoIPModem instances calling
each other** (both acting as DCEs) — a single instance talking to a
plain phone, as used for every live test so far this project, cannot
exercise this at all, since a phone never transmits V.8/CM/JM back.

**Live-tested between two real VoIPModem instances over Asterisk
(2026-10-17, same day) — reaches `Negotiated` on both sides, but not
reliably yet.** Two modem lines (`Line1`/COM4/ext. 1004,
`Line2`/COM6/ext. 1003, one process, one com0com pair per line) were
registered against the real Asterisk server and dialed from one to the
other. First attempt stalled forever: `Line1` never detected JM at all.
Added temporary diagnostics (`fprintf` probes into
`FeedSyncedDemodulator`/`ReceiveMenu`, removed once done) and found a
real bug, distinct from the two loopback-only bugs above: RTP/jitter-
buffer latency (observed ~400ms) means the call DCE can still be
*receiving* the answer DCE's already-stopped ANSam well after the
answer DCE's own state machine has moved on to transmitting JM. The
existing frequency-selective sync gate doesn't catch this, because the
audio genuinely is absent at JM's own frequencies right up until the
crossover moment — sync fired right on that transitional crossfade
instead of JM's true onset, permanently misaligning the receiver (no
clock recovery to self-correct). Fixed with an additional,
protocol-specific gate (`FeedSyncedDemodulator`'s new
`competingHz`/`competingThreshold` parameters): the call DCE's JM sync
also requires ANSam's own 2100Hz energy to have genuinely died down
(tightened to the same strict silence threshold used for "is there a
real signal at all," not the looser "is ANSam present" detection
threshold) before it's allowed to lock on. With that fix, **the full
handshake reached `Negotiated` on both sides of a real call for the
first time** — `CallAudioBridge: V.8 handshake negotiated` logged on
both `Line1` and `Line2`.

That said, it is **not reliable across repeated attempts** — this is
the known, already-documented lack of real symbol-timing/clock
recovery (see `V8Handshake.h`'s class comment), now confirmed to bite
under genuine real-world jitter, not just a single contrived scenario.
Observed on repeat dials: the JM menu Line1 decoded was self-consistent
(matched twice) but garbled -- it logged `selected mode: unknown`
(no usable modulation-modes field) while Line2 correctly resolved
`V.21` from Line1's CM. On another attempt, even CM-matching itself
(previously 100% reliable, since it's preceded by genuine silence)
stalled after only one match and never progressed. All of this is
consistent with one root cause: a one-shot "sync to the first loud
sample" heuristic with no actual bit-clock tracking cannot reliably
survive real audio-path jitter and tone-transition artifacts, even
after patching the one specific failure mode (lingering ANSam) found
first. A real fix needs actual symbol-timing recovery (e.g. an
early-late gate or a Mueller & Müller timing-error detector feeding a
PLL that continuously tracks the transmitter's bit clock), not another
one-off heuristic patch -- and V.32bis/V.34's actual data-mode
modulation will need this far more than V.8's slow 300-baud signaling
does, so this is worth solving properly before going further, not
patching around again.

**Real symbol-timing recovery implemented in `FskDemodulator`
(2026-10-18)**, per the above. This is a classic early-late gate
feeding a PI loop filter, not another one-off heuristic: a few samples
after each bit is decided, the same winning tone (whichever of mark/
space won) is re-measured in a window shifted slightly earlier and
slightly later than where this object actually placed it
(`timingDelta_`, ~1/6 of a bit). The resulting energy difference is the
classic early-late timing-error signal. A proportional term
(`kProportionalGain`) folds a quick phase nudge straight into the
existing fractional `bitLengthRemainder_` accumulator (the same
mechanism `FskModulator` already uses for its own +-1 sample/bit rate
compensation); an integral term (`kIntegralGain`) separately
accumulates into `estimatedRateErrorSamplesPerBit_`, a persistent
correction to the *average* bit length itself. The integral term turned
out to be essential, not a nice-to-have: a first attempt using only a
proportional nudge converged to a nonzero steady-state phase error
against a sustained rate mismatch (textbook P-only control), which was
enough to desync decoding by bit ~90 of a 400-bit test stream. Adding
the integral term fixed it -- confirmed in `FskTests.cpp`'s
`Fsk_SymbolTimingRecovery_TracksSlowBaudMismatch`, which simulates the
honest source of drift in this system (there's no mismatched crystal --
both ends compute at the same nominal 8kHz; see `DspTypes.h` -- but a
VoIP jitter buffer's own accelerate/expand concealment genuinely plays
audio back very slightly faster or slower than it was recorded) by
modulating 0.6% off the demodulator's nominal baud rate and requiring
correct decoding throughout a long stream. That test was written to
fail first: it was verified to fail with the loop gain zeroed out
(severe byte loss from accumulated drift) before being left in its
passing, corrected form -- the standard a unit test needs to actually
prove a fix matters, not just exercise code. A simpler first attempt at
this test (a single fixed phase offset injected at the very start of a
stream, modeling a lazy sync landing mid-bit) was tried first and
**found not to need correction at all** -- this demodulator's plain
threshold comparison is already fairly tolerant of a static sub-bit
misalignment; the discriminator curve's genuine weakness is tracking
a *sustained* rate error, not absorbing a one-time offset, which is
part of why the real bug (below) is about jitter-buffer-style drift,
not just "which sample did the lazy sync start on."

**Re-tested live** against the same two-instance setup
(`Line1`/COM4/ext. 1004 calling `Line2`/COM6/ext. 1003) immediately
after this change: 5 consecutive dial/hangup/redial cycles, all 5
reached `Negotiated` on both sides, and -- unlike the previous
session's runs -- every single one correctly logged
`selected mode: V.21` on *both* lines (the earlier `selected mode:
unknown` garbled-decode and the CM-matching stall described above did
not recur in any of the 5 attempts). 5 clean runs isn't a rigorous
reliability bound and this still isn't `V8Handshake`'s own lazy
"sync on first loud sample" edge-detection heuristic being replaced --
that heuristic still decides *when* a demodulator starts existing at
all; what changed is that once it exists, it now actually tracks the
real bit clock instead of assuming its own start was perfectly aligned
forever. But going from roughly one reliability bug surfacing per
2-3 live attempts to 5-for-5 is a real, measurable improvement, not
just a plausible-sounding mechanism.

Not yet done — this is genuinely a handful of increments into a large
effort, not a near-complete one:
- `[Modem.Line1]`/COM4's Access Denied — turned out to just be a stray
  PuTTY session sitting on the wrong COM port during the last live test,
  not a real bug. Should be a non-issue next time.
- The far-end-hangup bug above. (Incidentally re-confirmed this session:
  killing `VoIPModem.exe` to restart it during live testing leaves
  Asterisk's channel for the busy extension in a stuck state for
  roughly 15-25 seconds before it clears and the next dial succeeds —
  consistent with the same no-BYE-on-exit theory, not new evidence, but
  worth noting since it was hit repeatedly this session.)
- **Real symbol-timing/clock recovery** — implemented in
  `FskDemodulator` (early-late gate + PI loop, see above) and
  re-verified live (5/5 clean negotiations). What's still open: this
  tracks drift once a demodulator exists, but `V8Handshake`'s own
  lazy "sync on first loud sample" edge-detection heuristic (deciding
  *when* to construct one) is unchanged -- and V.32bis/V.34's actual
  QAM data modulation will need genuine carrier/clock recovery at a
  much higher symbol rate than V.21's 300 baud, which this specific
  implementation (tuned for V.21's Goertzel-threshold FSK decisions)
  doesn't address.
- CI (optional per spec, genuinely skipped, not just deferred).
- Retry/timeout handling in `V8Handshake` — right now a handshake that
  never completes just sits in `InProgress` forever.
- Basic QAM constellation mapping/demapping and a self-synchronizing
  scrambler/descrambler (shared infrastructure V.34 needs regardless of
  the harder pieces on top).
- Carrier recovery and an adaptive equalizer.
- **V.34 line probing, started (2026-10-18)**: `V34Crc`/`V34Info0`
  implement the bit-level CRC (clause 10.1.2.3.2) and INFO0 sequence
  codec (Table 14/V.34) confirmed against the real ITU-T V.34 (02/98)
  text (its public PDF from itu.int, same `pdftotext` approach as V.8).
  This project's own capabilities are represented honestly-minimal
  (`MakeDefaultLocalInfo0()`): no optional symbol rates, no power
  reduction, no 1664-point constellations. Deliberately scoped to just
  INFO0's bit layout in isolation, the same discipline used for
  `V8Message` before `V8CallMenu`/`V8Signal`/`V8Handshake` existed.
  Honest caveat on the CRC itself: clause 10.1.2.3.2's Figure 14 is a
  circuit-diagram image, not machine-readable text, so the exact
  shift-register wiring was inferred (reflected CRC-16/X-25-family
  algorithm for that polynomial/preset) rather than confirmed
  pixel-by-pixel; it's verified against the independent, external
  standard CRC-16/X-25 test vector, not yet against a real V.34 modem's
  actual INFO sequences.
  **DBPSK modulation/demodulation added (same day)**: `DbpskModulator`/
  `DbpskDemodulator` implement 10.1.2.3.1's actual modulation -- a new
  technique for this codebase, differential *phase* rather than
  frequency (FSK) or amplitude. The carrier runs continuously (built on
  `Dds`, which gained an `AddPhase()` method for the discrete 180-degree
  symbol jumps) and each symbol's bit is decided by correlating against
  the *previous* symbol, not an assumed absolute phase -- the point of
  using a differential scheme at all, since it doesn't need to know the
  channel's absolute phase offset. `EmitReferenceSymbol()` models
  10.1.2.3.1's "preceded by a point at an arbitrary carrier phase"
  requirement explicitly. 5 new tests (124 total), including one that
  specifically exercises the all-zero-bits/never-rotates path, not just
  whichever path a mixed pattern happens to hit most, and one confirming
  back-to-back sequences correctly share a single reference point per
  the spec's own wording. Still only a loopback-tested DSP primitive --
  not yet wired to `V34Info0`'s bits, not carrying the 2400Hz/1200Hz
  carrier-plus-guard-tone choice 10.1.2.3.1 actually specifies for the
  answer/call modem (that's `CallAudioBridge`/sequence-building's job,
  same separation `ToneGenerator`/`AnsamTone` keep from `FskModulator`),
  and with no symbol-timing recovery of its own yet (a new primitive,
  doesn't inherit `FskDemodulator`'s early-late gate automatically).
  **INFO0 wired end-to-end through DBPSK (same day)**: new
  `V34InfoSequence.h`/`.cpp` glues `V34Info0`'s bit codec to
  `DbpskModulator`/`DbpskDemodulator` — `EncodeInfo0ToSamples()` emits
  a real reference symbol plus the 49 data bits as actual audio
  samples at a caller-supplied carrier, `DecodeInfo0FromSamples()`
  reverses it (single-shot, not streaming, since INFO0 is always
  exactly 49 bits — no reason to decode it incrementally). The
  2400Hz-answer/1200Hz-call carrier split from 10.1.2.3.1 is exposed as
  `kAnswerModemInfoCarrierHz`/`kCallModemInfoCarrierHz` constants, but
  *choosing* which one applies, mixing in the guard tone, and anything
  about Phase 2 sequencing stay a `CallAudioBridge`/Phase-2 concern,
  same separation as everywhere else in this stack. 4 new tests (128
  total): round-trip through each carrier (default-local and
  all-fields-set `V34Info0` values), confirmation that decoding with
  the *wrong* carrier correctly fails (proving the carrier split
  actually matters, not just cosmetic), and a truncated-audio-buffer
  rejection. Still not wired into `CallAudioBridge`/`V8Handshake`
  themselves, and still inherits DBPSK's own lack of symbol-timing
  recovery.
  **Tone A/Tone B generation added (same day)**: new
  `V34ProbingTones.h`/`.cpp` implements 10.1.2.1 (Tone A, 2400Hz,
  answer modem, 1dB below nominal power, plus a continuous 1800Hz
  guard tone at full nominal power that never reverses) and 10.1.2.2
  (Tone B, 1200Hz, call modem, no guard tone specified in that clause's
  text) confirmed against the real Recommendation text re-fetched for
  this increment (same `pdftotext` approach, see below). Both tones
  reuse `Dds::AddPhase()` for their 180-degree reversals, the same
  mechanism `DbpskModulator` already uses. 7 new tests (135 total),
  verifying: constant frequency/amplitude with no reversal, a
  `ReversePhase()` call actually flips a local I/Q correlator's sign
  (the same differential check `DbpskDemodulator` relies on, just done
  directly in the test since no detector exists yet), two reversals
  cancelling back to the original phase, Tone A's -1dB/nominal
  amplitude ratio against its guard tone, that reversing Tone A's phase
  leaves the guard tone's phase alone, and that Tone B has no 1800Hz
  guard-tone energy at all.
  Generation only, deliberately -- same split kept for
  `FskModulator`/`FskDemodulator` and `DbpskModulator`/
  `DbpskDemodulator` before them. Still NOT done, and genuinely harder
  than generation: *detecting* a phase reversal in a live incoming
  tone and measuring exactly *when* it happened (round-trip-delay
  measurement is nothing but that timestamp, per 11.2.1.1/11.2.1.2's
  "minus 40 ms" arithmetic -- so a sloppy detector directly produces a
  wrong RTD estimate, not just a slightly-late bit decision the way a
  symbol-clocked demodulator can tolerate). Re-read clause 11.2 in full
  for this increment (its PDF, freely downloadable from itu.int, same
  UBC-hosted mirror copy used earlier this phase, converted via
  `pdftotext`) to confirm the exact sequencing before touching it next:
  the 75ms silent Phase-1-ending period, each side's INFO0/Tone
  send-then-detect ordering, the 40ms±1ms reversal-echo delay rule
  used symmetrically by both sides, the 10ms post-reversal hold, and
  each side's own retry/timeout fallbacks (11.2.2) -- none of that
  state machine is implemented yet, nor is round-trip-delay
  measurement itself.
  Not yet done, in rough order: a phase-reversal detector with
  real timing-accuracy guarantees (needed before round-trip-delay
  measurement means anything), INFO1a/INFO1c (they carry *computed*
  line-probing results this codebase can't produce yet), the actual
  L1/L2 multitone probe signals and their channel-response analysis,
  and the Phase 2 procedure's sequencing/timing/retry state machine
  (11.2) that ties all of the above together. Then: shell mapping,
  trellis encoding and Viterbi decoding, rate negotiation.
- Any interoperability test against a real modem, physical or
  software — nothing in this phase has been claimed as "working" per
  CLAUDE.md's rule against claiming standard support without
  implementation and interop tests.

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

**Target confirmed: V.34, not `docs/PROJECT_SPEC.md`'s literal "modest
target" of V.32bis (2026-10-17).** Worth recording why, since the two
docs could otherwise read as contradicting each other: V.32bis doesn't
need V.8 at all (pre-V.8 modems negotiated it directly via their own
tone/training sequence), but V.34 has no call-setup of its own -- it
*requires* V.8 to even begin. So the full V.8 handshake built this
session isn't scope creep over a V.32bis target; it's a hard
prerequisite for the real target, which is V.34. `V8CallMenu` currently
only advertises V.21 (what's actually implemented), not because V.8
itself is more limited than that -- the same V.8 envelope will carry
V.34 once V.34's modulation is built. This doesn't block an earlier,
smaller win along the way: V.32bis could still be implemented first
and offered through the same V.8 menu (real modems that speak V.8 will
happily negotiate down to it), without needing V.32bis's own separate
pre-V.8 handshake path.

Phases 1-3 are done and verified live. Phase 4 is underway from scratch,
targeting V.34. So far, live-tested: the tone-probe increment (a real
call confirmed the generated tone survived the SIP/RTP/G.711/FXS path),
then the role-correct V.21-rate FSK data link (`FskModulator`/
`FskDemodulator`, wired into `CallAudioBridge`), including a real
CPU-runaway bug found live and fixed (see above) and re-verified stable
for 3+ minutes of real call time.

Deliberately deferred (logged above, not forgotten): the far-end-hangup
bug, where hanging up from a physical phone doesn't tear down the call
on our side. Diagnosing it needs checking Asterisk's own channel state
while VoIPModem.exe is still running and stuck — not done correctly
yet — but the user has explicitly chosen to continue Phase 4 DSP work
first and come back to it.

The real ITU-T V.8 text has now been obtained and read (see above), and
the full handshake is implemented on top of it: the octet codec
(`V8Message`), menu assembly (`V8CallMenu`), the CI/CM/JM/CJ bit
sequences and preamble search (`V8Signal`), ANSam (`AnsamTone`), and
the clause 8 state machine itself (`V8Handshake`). It reaches
`Negotiated` on both sides in a lossless in-process loopback simulation
(8 new tests, 105 total) — including two real bugs a code review alone
would not have caught (a receiver clock-phase misalignment, and a
premature-partial-menu-decode bug), both found by that same loopback
test and fixed. It's wired into `CallAudioBridge`, replacing the FSK
counter-byte probe, advertising only V.21 (the one modulation this
codebase actually has working).

**That live test has now happened** (see above): two real VoIPModem
instances, calling each other over the real Asterisk server, reached
`Negotiated` on both sides at least once. It also found a real bug
(lingering-ANSam false sync) that got fixed, and surfaced the deeper
issue clearly: **the handshake was not reliable over real audio without
actual symbol-timing/clock recovery.** That's been implemented now (an
early-late gate feeding a PI loop filter in `FskDemodulator` — see
above) and re-verified live: 5/5 clean negotiations, no more garbled
mode decodes or CM-matching stalls across repeat attempts. The
remaining Phase 4 work is: retry/timeout handling in `V8Handshake`, the
deferred far-end-hangup bug, and then the much larger remaining climb
to actual V.32bis/V.34 modulation (QAM, scrambler, carrier recovery,
equalizer, trellis coding) — V.8 only negotiates which of those to use
next; it doesn't implement any of them, and genuine carrier/clock
recovery at V.32bis/V.34's actual symbol rates and constellations is a
separate, harder problem than this V.21-tuned fix solves.
