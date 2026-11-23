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
  **Phase-reversal detection added (same day)**: new
  `TonePhaseReversalDetector.h`/`.cpp` fills the gap flagged above --
  it's what makes the generated tones actually useful for round-trip
  delay. Design: a sliding-window correlator against a local reference
  carrier (never reset, same idea `DbpskDemodulator` already uses),
  updated one sample at a time rather than in non-overlapping blocks,
  so detection resolution is roughly a sample instead of a whole
  window. Compares the sliding window's (I, Q) against a baseline
  captured once the window first fills, and again right after each
  detected reversal, so a *second* reversal is judged against the
  post-first-reversal steady state rather than the original phase --
  matching 10.1.2.1/10.1.2.2's own "A to A', A' to A" wording. A
  magnitude gate avoids trusting a sign decision during silence/noise,
  and a dead time (one window's worth of samples) after each detection
  stops the transition itself, which takes about a window to fully
  flush through the sliding correlator, from being mistaken for extra
  reversals.
  6 new tests (141 total): no false positives over a long steady tone
  or over silence, a single reversal detected within the expected
  +-(window/2) timing tolerance of where it was actually inserted (not
  a loosened tolerance -- it's exactly the resolution limit the
  windowed-correlator design implies), two sequential reversals both
  detected at their correct respective indices, chunked-delivery
  parity with whole-buffer delivery (same discipline as
  `Dbpsk`/`Fsk`'s own chunked-delivery tests), and -- the one
  specifically worth calling out -- correctly detecting a Tone A
  reversal with its 1800Hz guard tone mixed in and additively present
  the whole time, proving the 2400Hz-targeted correlator isn't thrown
  off by that other, never-reversing energy.
  Still only a bare detection primitive, not yet connected to a real
  round-trip-delay *calculation* (RTDEa/RTDEc = the time between two
  specific reversals, each side's own, minus 40ms, per
  11.2.1.1.4/11.2.1.2.4) or to clause 11.2's sequencing/timing/retry
  procedure that decides when to listen for which tone, when to send a
  reversal of its own, and what to do if nothing is detected in time.
  **RTD calculation and L1/L2 generation added (same day)**: new
  `V34RoundTripDelay.h`/`.cpp` implements 11.2.1.1.4/11.2.1.2.4's exact
  arithmetic (interval between two reversal sample-indices, minus the
  fixed 40ms echo delay) on top of `TonePhaseReversalDetector`'s
  output -- 4 tests confirming the 40ms subtraction and a non-zero
  round trip. New `LineProbingSignalGenerator.h`/`.cpp` implements
  10.1.2.4's L1/L2 multitone probe signals (Table 17's 21 cosines, 150Hz
  spacing, 150-3750Hz, four frequencies omitted, each at a 0/180-degree
  initial phase) -- re-extracted from the real Recommendation text in
  *raw* `pdftotext` mode specifically, because layout mode mangles this
  particular table's column alignment (same discrepancy already found
  and resolved this way for the CRC work earlier this phase). Two
  honest implementation choices, neither spelled out by the text
  itself and NOT confirmed against a real V.34 modem: (1) equal power
  per tone, normalized so the 21-tone sum's total average power
  matches a single reference tone at the given nominal amplitude; (2)
  correcting for `Dds::NextSample()`'s sin-basis output against
  Table 17's cos(2*pi*f*t + theta) definition with a constant
  per-tone +90-degree `AddPhase()` offset -- an easy mistake to miss
  entirely, since it wouldn't show up in a simple magnitude check at
  all, only in a receiver doing coherent phase analysis later. 6 tests,
  including one built specifically to catch that exact sin/cos mistake
  by checking which of a tone's correlated (I, Q) components carries
  the energy, not just how much is present; all 21 tones also measured
  exactly leakage-free, by choosing a 1280-sample (160ms) test block
  length that happens to be an exact integer number of cycles of every
  Table 17 frequency at once.
  **Nothing above produces a round-trip-delay estimate from a live
  call yet** -- `V34RoundTripDelay` only does arithmetic on sample
  indices a caller already has in hand. 151 tests total.
  **Phase 2 state machine added (same day): first real RTD measurement
  from simulated live call audio.** New `V34Phase2Handshake.h`/`.cpp`
  drives one side (call or answer DCE) of clause 11.2's actual
  sequencing/timing procedure across real frame-by-frame audio -- same
  `ProcessFrame` architecture as `V8Handshake`, reusing its
  lazily-construct-the-detector-at-the-decision-point pattern. Also
  added: `StreamingInfo0Receiver` (`V34InfoSequence.h`/`.cpp`) -- the
  streaming counterpart `DecodeInfo0FromSamples` was missing, searching
  a growing demodulated bitstream for INFO0's frame sync via
  `V8Signal::FindBitPattern` reused directly rather than reinvented.
  Honestly scoped to 11.2.1.1.1-.4 (call modem) and 11.2.1.2.1-.5
  (answer modem): the INFO0 exchange and the first full Tone A/Tone B
  reversal exchange that produces each side's own RTDEc/RTDEa. The
  answer side implements through .2.5, one step further than its own
  RTD calculation (.2.4), because the call side's *second* Tone A
  reversal (needed for .1.4) is the answer side's own .2.5 action --
  both sides' RTD calculations are two sides of the same two
  40ms-delayed reversal "echoes" sent in direct response to each other.
  3 new tests (156 total): both sides reach a measured RTD and learn
  each other's INFO0 over a zero-extra-delay loopback, and -- the one
  that actually exercises the arithmetic -- both sides compute a sane
  RTD close to a deliberately-injected, non-trivial simulated line
  delay (50ms each way, via a small per-direction FIFO `DelayLine`).
  That last test's first attempt failed by exactly one frame's worth
  of round-trip time (40ms) -- not an implementation bug, but a wrong
  expected-value formula in the test itself: this synchronous-loopback
  harness has its own inherent one-frame latency (side A's TX from
  iteration N only reaches side B's RX at iteration N+1), on top of
  whatever's explicitly injected, the same way any real framed-audio
  pipeline has a floor on round-trip latency -- fixed by accounting
  for that in the test's expected value, not by loosening the
  implementation.
  Two honest simplifications, documented in the class's own header:
  (1) this implementation restarts each side's Tone A/B generator at
  phase 0 when TX switches from DBPSK (INFO0) to a plain tone, where a
  real analog modem's transmitter keeps one continuous-phase carrier
  through that transition -- harmless here since
  `TonePhaseReversalDetector` only ever judges *relative* phase change,
  but not literally the same signal a real modem would put on the
  line; (2) the RX switch from decoding INFO0 to watching for a tone
  reversal happens on a whole-frame boundary, not the exact sample
  INFO0 finished arriving -- costs the detector's baseline-capture
  window a slightly later start, not an error in any RTD value actually
  computed.
  **L1 (just L1, not L2) wired in (same day).** Right after the
  answer DCE's .2.5 reversal hold completes, it now transmits L1 for
  its fixed 160ms (`LineProbingSignalGenerator::GenerateL1Samples`)
  instead of going straight to silence; the call DCE, once its own
  RTDEc is measured, detects L1's arrival (a plain Goertzel presence
  check at one of its own Table 17 tones, 150Hz -- *not* a
  channel-response measurement, just "has L1-shaped energy started")
  and tracks its 160ms window, exposed via `GetHasReceivedL1()`. 1 new
  test (157 total): both sides run the full exchange and the call DCE
  ends up reporting L1 received.
  **L2 and the entire rest of Phase 2, resolved and implemented
  (2026-10-19), user delegated the "local echo" decision with one
  constraint: behave exactly like a real modem would.** Re-derived the
  FULL event sequence from the numbered clauses (11.2.1.1.1-.8,
  11.2.1.2.1-.9) rather than guessing: "received the local echo of
  L2" turns out to be the SENDER's own near-instantaneous, local-only
  echo of what it's transmitting -- on real hardware this fires at
  essentially the same instant as "started transmitting" (the
  local-loop delay is negligible against the spec's own 550ms+RTD
  budget). Modeled here as satisfied immediately once L2 TX starts,
  which leaves the OTHER half of each AND'd condition -- detecting the
  counterpart's tone -- as what actually gates each side in practice,
  implemented for real with the spec's own 550ms+RTD figure kept as
  the exact safety cap. This is "behaving like a real modem" in the
  sense that matters: the real gating behavior (wait for the
  counterpart, capped by a timeout) is faithfully reproduced; only the
  physically-unmeasurable near-zero local-loop delay is treated as
  zero, which is what it would measure as on real hardware too.
  Once that unblocked the sequencing, `V34Phase2Handshake.h`/`.cpp`
  were extended to cover the WHOLE error-free procedure: the second
  Tone A/B reversal exchange, both sides' L1/L2 transmit (now
  including L2, gated as above) and receive, and the INFO1c/INFO1a
  exchange that hands off into Phase 3. `V34Phase2Outcome` gained
  `Phase2Complete`; new `GetRemoteInfo1c()`/`GetRemoteInfo1a()`
  getters. New `V34Info1.h`/`.cpp` implements clauses 10.1.2.3.4/
  10.1.2.3.5 (Tables 15/16) -- INFO1c (109 bits) and INFO1a (70 bits),
  carrying Phase 2's actual *results* (power reduction, pre-emphasis
  filter, projected data rate per symbol rate, a 10-bit two's-complement
  1050Hz frequency-offset measurement). `MakeDefaultLocalInfo1c()`/
  `MakeDefaultLocalInfo1a()` honestly report projected data rate 0
  ("cannot be used") for the only symbol rate this project ever claims
  (2400 baud) -- not because any measurement found the line bad, but
  because this codebase has no trellis encoder, QAM mapper, or
  adaptive equalizer, so no data rate is achievable regardless of
  line quality; the frequency-offset field uses the spec's own -512
  "not measured" sentinel, since no real L1/L2 channel-response
  analysis exists either. `V34InfoSequence.h`/`.cpp` extended with
  the matching DBPSK transport (`EncodeInfo1cToSamples`/
  `EncodeInfo1aToSamples`, `StreamingInfo1cReceiver`/
  `StreamingInfo1aReceiver`), sharing one `FindAndDecodeFrame<T>`
  template with `StreamingInfo0Receiver` instead of tripling the
  search loop.
  Implementing this surfaced and fixed two real bugs worth recording:
  (1) `toneADetector_`/`toneBDetector_` (each reused across multiple
  reversal events over the full procedure) were only fed RX samples
  during the phases where their events mattered, silently desyncing
  their internal sample clock from elapsed real time across any
  frame where they went unfed -- fixed by feeding both unconditionally
  every frame for as long as they exist, dispatching on event count
  instead of phase; (2) the answer DCE's Tone A generator was only
  constructed inside the `AwaitingInfo0c` phase's own TX case, but RX
  could advance past that phase before TX ever got there, leaving a
  later phase calling into an empty `optional` -- fixed by draining
  each side's TX queues unconditionally before the phase switch rather
  than gating that draining by phase, and by moving the first
  reversal's scheduling condition into the shared tone-generation
  function so it keeps getting checked across the phase boundary that
  used to lose it. Both were caught by two new end-to-end tests
  (`V34Phase2Handshake_FullLoopback_ReachesPhase2Complete`,
  `..._WithSimulatedLineDelay_ReachesPhase2Complete`) that run the
  ENTIRE procedure to `Phase2Complete` on both sides -- including one
  with a real simulated 100ms round-trip line delay -- not just
  through the first RTD measurement like the earlier tests did. 188
  tests total.
  **Wired into `CallAudioBridge`, opt-in only (same day)**: once
  `V8Handshake` reaches `Negotiated` with V34Duplex/V34HalfDuplex
  selected, `CallAudioBridge` now hands the rest of the call's audio
  to a `V34Phase2Handshake` instance, logging RTD and L1 milestones.
  Gated behind new `ModemInstanceConfig::enableV34Probing`
  (`EnableV34Probing=1` in a `[Modem...]` ini section), default false
  -- this also controls whether the local V8 menu advertises
  V34Duplex at all. Deliberately NOT on by default: a real
  V.34-capable modem calling in would commit to training this
  codebase can only follow through the first RTD/L1 exchange, likely
  hanging or failing the call instead of cleanly negotiating down to
  V.21 the way it does today. Meant for a controlled test between two
  of this project's own instances, not for an instance taking real
  inbound calls. 2 new tests (159 total) cover the ini parsing
  (defaults false, parses an explicit `1`); the wiring itself has NOT
  yet been live-tested over a real call -- doing that (with this flag
  enabled only on the test rig's two instances) is the obvious next
  step, since it would be the first time any of this phase's V.34 work
  runs against real Asterisk/RTP jitter rather than an idealized
  sample-perfect loopback.
  **Phase 2 is now implemented end to end for the error-free case --
  this is the honest ceiling of that claim.** Still not implemented:
  this implementation's own L1/L2 channel-response *analysis* (turning
  received L1/L2 audio into the actual distortion/noise measurements
  INFO1 is supposed to carry -- right now both sides only ever report
  the honestly-empty defaults above, never a real measurement), and
  all of clause 11.2.2's retry/recovery procedure (the same
  honestly-deferred gap `V8Handshake` itself still has -- nothing here
  times out or retries if a step never completes; a real line's noise
  corrupting a frame, not just an idealized loopback, has never been
  exercised). Only after that: shell mapping, trellis encoding/Viterbi
  decoding, QAM constellation mapping, the scrambler/descrambler,
  carrier recovery, and an adaptive equalizer -- the harder, genuinely
  multi-month-scale remainder of a real V.34 data-mode modem, without
  which no projected data rate in INFO1 could ever honestly be
  nonzero regardless of line quality. None of this has been run over a
  real call, and no interoperability test against a real V.34 modem
  exists -- per CLAUDE.md, V.34 is NOT claimed as supported.
  **First Phase 3/4 primitive: the QAM superconstellation (2026-10-19).**
  New `V34Constellation.h`/`.cpp` implements clause 9.1's 1664-point
  superconstellation (Figure 5's quarter-constellation + clockwise
  90/180/270-degree rotations). Figure 5 is a literal scatter-plot
  image, not a text table -- unlike every other table/figure this
  phase has extracted so far -- so it couldn't be read off directly.
  Extracted via `pdftotext -table` (which aligned the image's point
  labels into a column grid far more reliably than `-layout` did for
  this specific figure) and cross-validated against the spec's own
  stated ordering rule end to end (smallest magnitude first, ties
  broken by greatest imaginary component): all 416 points satisfy it
  with zero exceptions, which would be astronomically unlikely by
  chance if the column mapping were wrong. Still NOT confirmed
  pixel-by-pixel against the actual image or against a real V.34
  modem. 12 new tests (200 total). This is one isolated building
  block, nothing more -- the scrambler (clause 7), shell mapping,
  trellis encoding/Viterbi decoding, carrier recovery, and an adaptive
  equalizer are all still unimplemented, and Phase 3 itself (clause
  11.3, equalizer/echo-canceller training) needs all of them together
  before any of its own sequencing could be honestly attempted -- it
  is NOT just a sequencing extension the way Phase 2 was.
  **Second and third Phase 3 primitives: the scrambler and TRN symbol
  generator (2026-10-20).** New `V34Scrambler.h`/`.cpp` implements
  clause 7's self-synchronizing scrambler/descrambler exactly as
  specified (GF(2) division by the generating polynomial: call mode
  GPC = 1 + x^-18 + x^-23, answer mode GPA = 1 + x^-5 + x^-23),
  re-fetched and quoted verbatim from the Recommendation text rather
  than from memory. New `V34Trn.h`/`.cpp` implements clause 10.1.3.8's
  TRN signal (the actual echo-canceller/equalizer training burst a
  real modem transmits, and what a person listening to the line would
  hear) by combining the scrambler (fed all-ones, per the clause) with
  the constellation module added the day before -- the 4-point and
  16-point cases are just the scrambled bits picked apart into a
  rotation index and (for 16-point) a base-point index into
  `kQuarterConstellation`. Both modules verified against hand-derived
  oracles: the exact first ~40 scrambler output bits were worked out
  by hand from the recurrence (not from running the code) and the
  implementation matches them bit for bit, for both tap sets; the TRN
  tests regroup that same oracle into symbols per the clause's bit
  weighting (`In = 2*I2n + I1n`, i.e. the second-in-time bit is the
  high bit) and confirm the generator reproduces the resulting point
  sequence exactly. Also tested: real round-trip (scramble then
  descramble recovers arbitrary data) and the actual self-synchronizing
  property (a descrambler that joins mid-stream with the wrong internal
  state still recovers correctly after `max(n1, n2) = 23` bits, with no
  shared initial state). 11 new tests (211 total).

  **This still does not make anything audible change on a real call.**
  `V34TrnGenerator` only produces the sequence of (I, Q) constellation
  points a modem would transmit -- integers, not a waveform. Turning
  that into something you could actually hear over RTP still needs,
  at minimum: a pulse-shaping QAM modulator driven at the negotiated
  symbol rate and carrier frequency (clause 9.3/9.4 -- not researched
  yet, and the harder of the two to get bit-exact since it likely
  isn't given as literal time-domain filter taps); clause 11.3's own
  Phase 3 sequencing (PP/TRN/J/MD signal exchange, mirroring how
  `V34Phase2Handshake` sequences Phase 2); and, to know when to
  honestly stop transmitting TRN rather than guessing a fixed timeout,
  some real notion of equalizer convergence -- which doesn't exist
  because the adaptive equalizer itself doesn't exist. Shell mapping,
  trellis encoding/Viterbi decoding, and carrier recovery are also
  still unimplemented. `CallAudioBridge` has not been touched by any of
  this and still stops at "received L1 (not yet implemented past this
  point)" on a real call.
  **Fourth Phase 3 primitive: a QAM pulse-shaping/carrier modulator
  (2026-10-20).** New `V34QamModulator.h`/`.cpp` turns a symbol
  sequence (e.g. `V34TrnGenerator`'s output) into a real passband audio
  waveform -- the first point in this phase where V.34 work produces
  actual sound rather than bits or integers. Re-checked the
  Recommendation text specifically for this: clause 5.2 (symbol rate,
  Table 1) and clause 5.3 (carrier frequency, Table 2) ARE normative
  and exact, and this module takes them as exact caller-supplied
  parameters. But clause 5.4.1 ("Transmit spectrum specifications")
  only gives a +/-1 dB power-spectral-density MASK (Figures 1/2, image
  graphs again, not filter taps) that any transmit filter must fit
  inside -- it does not mandate a specific filter. That is a confirmed
  gap in the normative text, not a research shortcut: grepped the full
  extracted spec text for "roll-off", "square-root", "transmit filter",
  "pulse shap*" and found nothing. So this module makes the same kind
  of implementation choice any real V.34 modem vendor had to make: a
  standard square-root-raised-cosine (SRRC) pulse with a caller-supplied
  roll-off/span. It has NOT been measured against the Figure 1/2 mask --
  that would need digitizing those curves first, the same problem
  Figure 5 was.
  Tested: the SRRC closed-form pulse's symmetry, peak-at-zero, and the
  value at its removable singularity (t = T/(4*beta)) against the limit
  approached from nearby points (not just "doesn't crash"); the actual
  Nyquist zero-ISI property via discrete autocorrelation of a sampled
  pulse (self-convolution has zero crossings at nonzero symbol-spaced
  lags, matching what a real receive-side matched filter/equalizer
  would rely on); output sample-count accounting; and that a modulated
  TRN burst has Goertzel-measurable energy concentrated at the real
  V.34 carrier frequency (1800 Hz for symbol rate 2400, Table 2) rather
  than being broadband. One finding worth flagging: a bare SRRC pulse
  is NOT itself zero-ISI (only SRRC matched-filtered with itself at the
  receiver is), so with a low roll-off the unscaled output can run to
  roughly 12x the chosen amplitude parameter -- there is no receiver in
  this codebase yet to undo that, so this is still only useful for
  generating a waveform to listen to or measure, not for a working
  link. 8 new tests (219 total). Still batch-only (one fixed symbol
  sequence in, one fixed waveform out) -- no streaming API, and still
  not wired into `V34Phase2Handshake` or `CallAudioBridge`. Shell
  mapping, trellis encoding/Viterbi decoding, carrier recovery, and an
  adaptive equalizer remain unimplemented; clause 11.3's own Phase 3
  sequencing still needs all of them before it can be attempted.
  **Fifth Phase 3 primitive: a QAM demodulator (2026-10-24), and a real
  wall found by actually testing it, not assumed.** New
  `V34QamDemodulator.h`/`.cpp` -- the receive-side counterpart to
  `V34QamModulator`: carrier downconversion + SRRC matched filtering,
  sampled at the symbol rate, turning a passband waveform back into
  (I, Q) estimates. Deliberately not a real receiver yet (no
  carrier-phase or symbol-timing recovery -- it assumes the caller
  already knows both exactly, true only in a lossless loopback), but a
  meaningful thing to verify now anyway: that the downconversion +
  matched-filter arithmetic itself is correct, the same "primitive
  verified in isolation" discipline as `Dds`/`GoertzelDetector` before
  `FskModulator`/`FskDemodulator`, and the same loopback-before-live-call
  approach `V8Handshake` used.
  **Verified it works, then verified where it stops working, both by
  measurement:** a loopback test (modulate a real 16-point TRN burst,
  demodulate the identical waveform, nearest-neighbor decode against
  the actual alphabet) decodes every interior symbol correctly at 400
  baud (20 samples/symbol at this codebase's fixed 8 kHz rate) -- the
  matched-filter math is right. The same test at V.34's own actual
  operating point, 2400 baud (only ~3.33 samples/symbol), decodes
  roughly 76% of symbols *wrong*. Chased this down before accepting it:
  confirmed it's a quadrature/discretization problem (the discrete sum
  is too coarse an approximation of the continuous matched-filter
  integral at this sample density), not an aliasing problem (the
  signal sits well inside the 8 kHz Nyquist bandwidth) and not fixed by
  a wider filter span (tried `filterSpanSymbols=32`, 4x the default;
  the error rate barely moved, from 216/284 to 211/284 wrong) -- only
  raising samples/symbol fixed it. 4 new tests (223 total), including
  one that deliberately pins the current ~76%-wrong number (see its own
  comment) so a future change has to explain why that number moved,
  rather than quietly loosening a tolerance until it passes.
  **Consequence for the real target**: `V34QamDemodulator`, as it
  stood, could not decode real V.34-rate symbols from real 8 kHz call
  audio -- it was not wired into `CallAudioBridge`. A real fix needed
  either a properly designed discrete-time (fractionally-spaced)
  matched filter in place of this direct closed-form evaluation, or an
  interpolation/upsampling stage ahead of it.
  **That fix, built and measured (2026-11-23).** Rebuilt
  `V34QamDemodulator` around the fractionally-spaced design: (1) digital
  downconversion, (2) a FIXED discrete-time FIR matched filter (taps
  sampled once from the SRRC closed form at `kSampleRateHz`, applied by
  ordinary integer-indexed convolution -- no fractional evaluation
  anywhere in this step), producing clean, genuinely baseband
  (bandwidth-limited) I/Q streams at the full sample rate, then (3) a
  Lanczos (windowed-sinc) fractional-sample interpolator to pick each
  symbol's value out of those streams at its own, generally non-integer
  sample position. This separates "filtering" (an ordinary, safe
  discrete convolution) from "resampling to the symbol instant"
  (interpolating an already-safely-band-limited signal), avoiding the
  previous version's mistake: evaluating `waveform(t) * h(t - t_k)`
  directly per symbol formed a PRODUCT of two band-limited signals,
  which can occupy close to the SUM of their bandwidths -- worked out
  numerically, enough to exceed the 8 kHz Nyquist limit even though
  each signal alone did not, explaining the earlier ~76% error rate as
  a real, previously-undiagnosed aliasing mechanism, not mere
  discretization noise.
  Measured, not assumed: on the same 16-point/300-symbol loopback, the
  redesign alone (still at the original rolloff of 0.125) cut the error
  from 216/284 (~76%) to 59/284 (~21%) -- a real improvement, but not a
  fix by itself. Widening `filterSpanSymbols` further (span 12, 16)
  barely moved it (51-54/~270 wrong), ruling out plain truncation as
  the remaining cause. A rolloff sweep at the real 2400-baud/1800 Hz
  parameters (span 8/12/16, rolloff 0.15 through 0.5) found a wide,
  robust range (~0.28-0.48) with **zero** decode errors on both a
  4-point (1000-symbol) and a 16-point (300-symbol) burst, bracketed by
  two narrow, explainable failure points at exactly rolloff 0.25 and
  0.5: both are where the RC pulse's own bandwidth
  (`(1+rolloff)*symbolRate/2`) puts the downconversion's image (at 2x
  the 1800 Hz carrier) close enough to alias back across the 8 kHz
  Nyquist boundary into the wanted band -- a real, identified resonance,
  not noise. **0.3 is the now-recommended rolloff** for any future code
  wiring this into a real V.34 Phase 3 signal at this symbol rate (both
  `V34QamModulator` and `V34QamDemodulator` must agree, since matched
  filtering requires it); the classes' shared 0.125 default constructor
  argument was deliberately left alone, since clause 5.4.1 still doesn't
  mandate a rolloff and nothing yet calls these classes without passing
  one explicitly. 4 new/rewritten tests (225 total): the 400-baud sanity
  check is unchanged, one test pins the real-but-reduced ~8.5% residual
  at the original rolloff, and two confirm zero errors at rolloff 0.3 on
  both constellation sizes.
  **Symbol-timing recovery added (2026-11-23), and a real architectural
  coupling found while testing it.** `V34QamDemodulator::Demodulate` no
  longer assumes the exact analytic symbol position -- it tracks it
  with a non-data-aided Gardner timing-error detector feeding a PI loop
  (same proportional+integral shape as `FskDemodulator`'s early-late
  gate, a different error signal). Tuning this exposed a real,
  measured acquisition-speed-vs-steady-state-noise tradeoff: a gain
  large enough to quickly correct a deliberate several-sample
  misalignment also random-walked an already-perfectly-aligned,
  multi-thousand-symbol burst by 2-3 samples from the Gardner TED's
  ordinary data-pattern-dependent noise alone (confirmed by watching
  the tracked position drift with the actual symbol rate matching
  exactly, i.e. nothing real to correct); a two-stage (fast-acquire,
  then-narrow) gain schedule, the standard fix for that tradeoff, was
  tried and measured not to help here (see below for why). Settled on
  a single small, steady-state-safe gain.
  **Measured result**: this tolerates a realistic symbol-clock
  mismatch -- tested at +/-20/50/100 ppm (generous real
  crystal-oscillator tolerances) over an 800-symbol burst with zero
  added decode errors. It does NOT fix not knowing where symbol 0
  actually starts: a deliberate 1-sample shift of the whole waveform
  still fails badly (most symbols wrong), and a genuine architectural
  finding explains why, not a tuning shortfall -- shifting the raw
  waveform also rotates the carrier phase used by the downconversion
  step (referenced to each sample's absolute index), and with no
  carrier-phase recovery anywhere in this codebase, that phase error
  alone is enough to break decisions regardless of timing accuracy.
  **Symbol timing and carrier phase are coupled in this design, not
  independent problems that can be solved one at a time** against a
  realistic unknown-start-time scenario -- confirmed by testing (the
  two-stage gain schedule mentioned above didn't help this case either,
  because the problem it hits is phase, not timing-loop bandwidth). 4
  new tests (227 total, all passing, 0 build warnings): realistic-drift
  tolerance, and the sample-shift/carrier-phase-coupling finding pinned
  as a known limitation. See `V34QamDemodulator.h`'s "SYMBOL-TIMING
  RECOVERY" section for the full account.
  **Update: carrier-phase recovery landed the same day, see below --
  it is no longer true that "no carrier-phase recovery exists at all."
  Still not wired into `CallAudioBridge`, and still not a real
  receiver**, for the reasons the entry right below explains.
  **External-softmodem alternative investigated and rejected, same
  day.** Before continuing with carrier-phase recovery, the user asked
  whether VoIPModem could skip writing V.34 DSP entirely by either (a)
  emulating an existing PCI softmodem chipset so a real Windows OEM
  driver does the DSP, or (b) reusing D-Modem, which already combines
  `slmodemd` with PJSIP to run real dial-up calls over SIP on Linux.
  Researched both before writing any code, per CLAUDE.md's "prefer
  established libraries... but verify, don't assume" discipline:
  - (a) would need a kernel-mode PCI bus/device emulator plus
    reverse-engineering an undocumented HSP chipset's register
    protocol -- categorically harder and different work than DSP, with
    no confirmed driver that even installs on the actual target
    Windows Server version.
  - (b) D-Modem (`AonCyberLabs`/`strozfriedberg`/`D-Modem`, GPL-2.0;
    `synexo` fork) was already on file as a Phase 4 reference (see the
    original library survey above) but re-checked directly against its
    own READMEs rather than trusting an older summary: the **original
    is dial-out only** -- its own Known Issues say "There is currently
    no support for receiving calls." The **`synexo` fork adds answering
    but calls it "buggy"** and warns it "might not make another call
    after it has made one" -- a real problem for a modem bank handling
    sequential calls. Its own best reliability claim ("V.34/V.90
    usually successful") was measured against a Cisco 2951 with digital
    modem cards and a **GPS-synced clock**, which lines up with this
    project's own hard-won finding that V.34-over-VoIP is extremely
    clock-sensitive (see the Gardner timing-recovery and carrier-phase
    work in this same section). It's Linux-only (32-bit build), and its
    only Windows angle is a Windows **guest** VM -- the opposite of what
    this project needs. Adopting it would mean a Linux VM/container as
    a hard runtime dependency, which conflicts with CLAUDE.md's Windows
    Server 2003 deployment constraint (Hyper-V isn't available there).
  **Decision: continue the from-scratch DSP path.** Not rejected by
  assumption -- investigated with real sources (direct README reads),
  and the from-scratch path was already measurably working piece by
  piece (V.8, V.34 Phase 2 handshake, QAM mod/demod, Gardner timing
  recovery) with no external VM or licensing dependency.
  **Scope clarified by the user, same day: VoIPModem is a server-side
  modem bank, not a PPP client or ATA/physical-modem replacement.** It
  terminates SIP + the modem DSP itself and hands the resulting data
  stream to the *same* Windows Server machine's own RRAS/PPP stack
  through a virtual COM port, the way a real hardware modem bank would
  -- Windows' existing RAS/PPP stack does the actual PPP session
  (LCP/IPCP/authentication/IP assignment), not VoIPModem. This doesn't
  change the DSP work still needed (real analog modems calling in still
  need a real, working V.34 receiver), but it does rule out ever
  building a custom PPP stack, a RAS dial-out client role, or any
  GUI/general-purpose-ATA ambition -- see `docs/PROJECT_SPEC.md`-level
  scope, not something to re-litigate.
  **Carrier-phase recovery added (2026-11-23), with a real,
  non-obvious bug found and fixed before it worked at all.**
  `Demodulate()` now also runs a non-data-aided, 4th-power
  ("Costas-like") phase-error detector feeding its own PI loop (same
  shape as the Gardner timing loop). First attempt was unusable: large,
  erratic, sign-flipping error even with zero real phase error to
  correct. Root cause, found by measurement: the 4th-power detector's
  "zero error" reference angle isn't always 0 -- it depends on the
  constellation's own intrinsic angle, and V34TrnGenerator's base point
  (`kQuarterConstellation[0]` = exactly `(1, 1)`, i.e. 45 degrees) puts
  a correctly-phased signal's 4th power at a CONSTANT 180 degrees, not
  0 -- landing right on atan2's +/-180-degree branch cut, where
  floating-point noise flips the sign essentially at random. Fixed by
  pre-rotating by that known reference angle before the 4th power, so
  the true lock point sits away from the cut.
  With that fixed, and gains tuned the same conservative way as
  Gardner's (small enough to stay silent on an already-correctly-phased
  signal -- a two-stage acquisition/tracking gain schedule was tried
  here too and, unlike for Gardner, still didn't help; a much larger
  acquisition-stage gain left the test case barely improved while
  breaking the already-correct ones the same way an oversized single
  gain did before), this loop: adds zero decode errors to every
  already-correct-phase test in the file; and, measured on the
  1-sample-shift scenario from the symbol-timing entry above (now
  re-run over a longer, 1500-symbol burst), genuinely and monotonically
  reduces decode errors over the burst -- ~95% wrong in the first
  third, ~53% wrong in the last third. Real, measured progress, not
  nothing -- but it does NOT converge fast enough to actually resolve
  that scenario within a realistic burst length. 4 tests updated/added
  (227 total, all passing, 0 build warnings).
  **Conclusion for both loops (at the time)**: symbol timing and
  carrier phase are no longer "one doesn't exist at all" blocked on
  each other, but neither loop, as currently tuned, can acquire a
  large/sudden initial misalignment quickly -- both are built for
  already-close tracking, not blind acquisition from a cold start. The
  realistic next step is a separate, faster acquisition mechanism, not
  further gain tuning on either loop.
  **Batch acquisition built the same day, and it corrected the above
  diagnosis along the way.** Added a one-shot, batch-averaged initial
  carrier-phase estimate (`EstimateInitialCarrierPhase`, computed from
  the burst's own first ~100 symbols, before the fine per-symbol loop
  runs at all) -- summing many symbols' 4th-power complex values first,
  then taking the angle of the sum, so the constant component
  reinforces while data-dependent noise partially cancels, the same
  coherent-averaging principle a matched filter already relies on.
  Verified directly: on the 1-sample-shift scenario this was built to
  fix, the batch estimate lands within about 0.2 degrees of the ideal,
  modulo-90-degree-reduced target -- essentially exact.
  **And yet end-to-end decode on that same scenario barely improved**,
  which is what corrected the diagnosis: with carrier phase now
  acquired almost exactly, the Gardner-recovered samples right after
  the acquisition window still only have about 1/7th the magnitude a
  correctly-timed symbol should. **Carrier phase was never the
  dominant bottleneck for this scenario -- Gardner's own cold-start
  timing lock is.** Ruled out re-attributing this to the (real, known,
  unresolvable) 90-degree rotation ambiguity 4th-power recovery leaves
  behind: decoding against all 4 possible rotations found the best one
  still ~39% wrong, not near 0% the way a clean rotation-ambiguity case
  would show. 1 test rewritten with the corrected diagnosis (227 total,
  all passing, 0 build warnings) -- see
  `V34QamDemodulatorTests.cpp`'s
  `Demodulate_SampleAlignmentShift_IsolatesGardnerAcquisitionAsTheRemainingBottleneck_KnownLimitation`
  and `V34QamDemodulator.h`'s "ACQUISITION" section for the full
  account, including why this version of the finding is trusted more
  than the last one: it is backed by an independent measurement of the
  acquisition estimate's own accuracy, not just an end-to-end decode
  number that could have several different causes.
  **Concrete next step, now correctly scoped**: symbol timing needs the
  same kind of fast, batch-based acquisition step carrier phase just
  got -- e.g. a coarse search over candidate sampling phases for the
  one that maximizes recovered signal magnitude, before handing off to
  Gardner for fine tracking -- not further gain tuning on either loop
  (tried multiple times now, for both loops, specifically for cold-start
  acquisition, and it does not work).
- **Hayes ATL/ATM (speaker volume/mode) and local audio monitoring,
  added (2026-10-19), separate from the V.34 work above.** User asked
  how to "hear the modem" the way a real modem's on-board speaker lets
  you hear dialing/negotiation. Two pieces:
  - `AtCommand.h`/`AtCommandParser.cpp`/`ModemStateMachine.cpp`/
    `ModemProfile.h` now recognize ATLn (speaker volume) and ATMn
    (speaker mode), returning OK and storing the value instead of
    ERROR -- these previously fell through to Unknown. Real-world
    motivation: Windows' own "Phone and Modem" control panel maps its
    Properties dialog's "Speaker volume" slider directly to ATLn, and
    many generic driver init strings include ATM1 or similar; a modem
    that errors on them breaks RAS/dialer compatibility. 5 new tests
    (164 total) across the parser and state machine, including
    `AT&F` restoring both to their factory defaults (L2, M1).
  - New `src/Lib/Audio/WindowsAudioOutput.h`/`.cpp`: a thin winmm
    (`waveOutOpen`/`waveOutWrite`, multi-buffered, polling `WHDR_DONE`
    instead of a callback) wrapper that plays PCM audio out the
    Windows default sound output device. `CallAudioBridge` mixes
    every frame's RX+TX audio (the same way a real 2-wire line
    acoustically combines both directions onto whatever's tapping it)
    and writes it there -- gated behind new
    `ModemInstanceConfig::enableLocalAudio` (`EnableLocalAudio=1` per
    `[Modem...]` ini section), default false, threaded through
    `SipAccountConfig`/`PjsipCallController::Impl` the same way
    `enableV34Probing` already is. Deliberately per-line and opt-in,
    not always-on (an earlier version of this increment tried
    always-on and the user caught the problem immediately): two
    VoIPModem instances calling each other on the SAME machine (the
    com0com test rig) would otherwise both open the one shared default
    device and mix into one indistinguishable stream -- enabling it on
    just one line at a time is the actual fix, not stereo panning or
    any other automatic trick. This is the literal answer to "hear the
    modem in Windows settings": there is no separate modem-specific
    settings page, the audio just appears as an ordinary Windows
    playback stream once enabled, so the *existing* Windows Sound
    settings/Volume Mixer (device choice, mute, volume) already
    control it, same as any other app's audio. 3 new `AppConfig` tests
    (167 total): defaults false, parses an explicit `1`, and -- the one
    that actually matters -- two `[Modem.X]` sections parse the flag
    independently of each other.
  Honest gap between the two pieces: ATL/ATM's *values* are accepted
  and stored, but `WindowsAudioOutput`/`CallAudioBridge` do not yet
  read them back -- playback is unconditionally "on" regardless of
  ATM's 0/1/2/3 setting, and at a fixed, unscaled level regardless of
  ATL. Wiring that properly means crossing the Core/SIP architectural
  boundary CLAUDE.md calls out (speaker mode lives in
  `ModemStateMachine`, the audio itself flows through
  `CallAudioBridge`, a layer apart) -- not attempted yet, and ATM's
  "on until carrier detected" semantics would also need real call-state
  awareness this class doesn't have.
  **Live-tested with an actual human ear (2026-10-19)**: user confirmed
  the V.8 handshake audio (ANSam/CM/JM/CJ) sounds "clean, exactly as
  I'd expect it to be" over the local two-instance test rig with
  `EnableLocalAudio=1` on one line. The one gap the user flagged
  listening to it: no dial tone or dialing sound before/during a call
  -- expected, since there's no physical analog line to produce either
  from, and explicitly called out by the user as something to "mock."
- **Dial tone + DTMF dialing sound, mocked (2026-10-19).** New
  `src/Lib/Dsp/DtmfGenerator.h`/`.cpp`: `GenerateDialTone()` (the
  classic North American 350+440Hz continuous dial tone) and
  `GenerateDtmfDigit()` (standard ITU-T Q.23/Bell System 503 DTMF
  frequency pairs -- row 697/770/852/941Hz, column
  1209/1336/1477/1633Hz), both built on the shared `Dds` oscillator, 6
  new tests (173 total) including Goertzel-verified frequency-pair
  correctness per digit. Purely cosmetic: this codebase already dials
  by putting the number straight into the SIP INVITE
  (`ICallController::Dial`), so none of this is sent anywhere or
  affects real call routing -- it only exists for a human listening
  locally. `ModemStateMachine` now exposes `LastDialedNumber()` (1 more
  test, 174 total); `ModemInstance` watches for the transition into
  `ModemState::Dialing` each loop iteration and, if
  `enableLocalAudio` is set, spawns a short-lived background thread
  (`PlayDialSequence`) that writes 1s of dial tone then each digit's
  DTMF tone (80ms on, 80ms gap) to its own `WindowsAudioOutput`
  instance, paced with real-time sleeps between writes so the digits
  are heard distinctly rather than dumped as one blob. Polls
  `stopRequested_` between digits so `Stop()` isn't blocked for the
  sequence's full ~1-3 second duration; a redial before a previous
  sequence finishes joins it first rather than overlapping two
  sequences on the same output. Not yet live-tested with an actual
  human ear (the V.8/V.34 audio above has been; this hasn't yet).
- Any interoperability test against a real modem, physical or
  software — nothing in this phase has been claimed as "working" per
  CLAUDE.md's rule against claiming standard support without
  implementation and interop tests.
- **Scope decision (2026-11-23): reach a real "two modems connect and
  talk" milestone via V.21 first, not by continuing to chase V.34's
  cold-start acquisition problem.** Per the user's explicit "we'll
  never finish if we try to perfect every routine" direction, the
  target for the next concrete milestone is wiring `V8Handshake`'s
  already-negotiated V.21 mode into a real FSK data-mode byte
  passthrough (serial port -> `FskModulator`/RTP -> far end's
  `FskDemodulator` -> serial port), using primitives
  (`FskModulator`/`FskDemodulator`) already proven stable on a real
  call earlier in Phase 4 -- not full V.34 data mode, which still needs
  the acquisition rework below first.
  **Built the same day.** `CallAudioBridge` now runs a real bidirectional
  V.21 data link once `V8Handshake` negotiates V.21 (the only mode it
  ever actually will, until V.34 data mode exists): `StartDataLink()`
  constructs an `FskModulator`/`FskDemodulator` pair with the same
  channel assignment `V8Handshake` already uses (calling party
  transmits channel 1/listens channel 2; answering party the mirror
  image), and `RunDataLink()` decodes arriving async-framed bytes into
  a queue and keeps a TX sample buffer topped up from a second queue,
  transmitting a continuous mark ("idle") tone between bytes exactly
  as a real async line does. Both queues are mutex-guarded and
  reachable from outside the class via new `EnqueueTxByte`/
  `DrainRxBytes` methods -- necessary because pjsua2's media callbacks
  run on PJSIP's own thread while the serial port is read on
  `ModemInstance`'s.
  `ICallController` gained `SendData`/`PollReceivedData` to carry this
  up through `PjsipCallController` (which forwards to the active
  call's bridge, following the same unlocked-`activeCall_`-access
  convention `Dial`/`Answer`/`HangUp` already use) and down to
  `NullCallController` (no-ops; it never has an active call).
  `ModemInstance::RunLoop` now branches on `ModemState::OnlineData`:
  serial bytes go to `SendData` instead of `CommandLineSession`, and
  `PollReceivedData` drains straight back out to the serial port every
  loop iteration. There is deliberately no `+++` escape sequence back
  to command mode yet -- the only way out of data mode is the call
  ending (`ATH`, or the far end hanging up).
  **Known, deliberate simplification**: `ModemState::OnlineData` (and
  the Hayes `CONNECT` it causes) is still driven purely by SIP media
  confirmation (unchanged from Phase 2), not by `V8Handshake` actually
  finishing -- a real modem's CONNECT means "the modem link is up",
  which here can lag SIP's own CONNECT by the second or so V8
  negotiation takes. This does not lose any bytes: they wait queued in
  the call's audio bridge until the data link activates (see
  `ICallController.h`'s own comment for the one narrow exception --
  a byte sent in the brief window before PJSIP's media-active callback
  constructs the bridge at all is dropped, not queued further back
  than that). Making CONNECT wait for the real modem link would be
  more accurate and is a reasonable future improvement, not attempted
  here to keep this increment to one concrete thing. All 227 existing
  tests still pass (0 build warnings); no new unit tests were added,
  consistent with this codebase's existing practice of verifying
  `CallAudioBridge`/`ModemInstance` live rather than in isolation (both
  require a real PJSIP media subsystem to even construct). **Not yet
  live-tested** -- that is the next step before this milestone can be
  called done, per CLAUDE.md's rule against claiming a standard/feature
  works without an actual interoperability test.
- **Architectural reference found for V.34's still-open acquisition
  gap, independent of the above: BinModem (github.com/CasualArclamp/
  BinModem, GPL-3.0, Rust) — read for architecture only, same as
  D-Modem/slmodemd earlier in this phase, never as a code source.** Its
  own design docs independently confirm VoIPModem's diagnosis (a
  symbol-spaced equalizer/Gardner combination cannot invert a channel
  null that lands at the worst sampling phase — documented in its own
  `docs/design/slow-modes/shared-dsp.md` as a known weakness of exactly
  that architecture) and show the actual fix it shipped: no blind
  acquisition stage at all. It uses the *known* TRN/PP training
  sequence (which VoIPModem already generates via `V34Scrambler`/
  `V34TrnGenerator`) to directly least-squares-solve a T/2 (half-symbol-
  spaced) equalizer across a small window of candidate alignments,
  reads carrier turn off that same solve's residual phase, and only
  then hands off to small tracking loops for residual drift — which
  work well specifically because they start already close, not because
  they can acquire from a cold start (the same lesson VoIPModem's own
  Gardner/Costas tuning attempts already learned the hard way). See
  [[reference_binmodem]] (project memory) for the full architectural
  account with file:line citations into BinModem's own source, kept
  there rather than duplicated here since it's reference material, not
  VoIPModem's own implementation history. **This is the recommended
  next V.34 architecture once the V.21 milestone above ships** — not
  more gain tuning on the existing Gardner/Costas loops, which has
  already failed twice for related reasons.

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

**Update (2026-10-24), since the above predates most of the actual
clause 10/11 (Phase 2/Phase 3) work landed since**: `V34Phase2Handshake`
(the Tone A/B + L1/L2 + INFO1a/INFO1c exchange) is implemented and
described in full further up this file; `CallAudioBridge` wires L1 in
and flags L2 as gated behind a config flag. Phase 3's own primitives —
scrambler (clause 7), TRN generator (10.1.3.8), and a QAM
modulator/demodulator pair (clause 5.2/5.3 symbol rate and carrier,
SRRC pulse shaping) — now all exist in isolation, each loopback-tested
before being combined with anything else, per this project's established
discipline. The concrete next blocker, found by actually measuring
the demodulator rather than assuming it would work, is described right
above under "Fifth Phase 3 primitive": `V34QamDemodulator`'s naive
closed-form matched filter cannot decode symbols at V.34's real 2400
baud over this codebase's fixed 8 kHz sample rate (~3.33 samples/symbol
is too coarse for its discrete quadrature), only at a lower,
well-oversampled baud rate.

**Update (2026-11-23): that blocker is fixed, measured, and now has a
recommended rolloff.** See "Fifth Phase 3 primitive" above for the full
account. `V34QamDemodulator` was rebuilt around a genuinely
fractionally-spaced design (fixed discrete FIR matched filter + Lanczos
fractional-sample interpolation, instead of evaluating the matched
filter per symbol via the closed-form pulse directly — the latter was
root-caused to an aliasing mechanism, not mere coarse quadrature: the
per-symbol product of two band-limited signals can occupy close to the
sum of their bandwidths, enough to exceed 8 kHz Nyquist even though
neither signal alone does). That alone cut the error rate at V.34's
real parameters from ~76% to ~21% (16-point burst) / ~8.5% (4-point
burst, both at the original 0.125 rolloff) — real, but not a full fix.
A rolloff sweep at the real parameters found a wide, robust range
(~0.28-0.48, with 0.3 recommended) that decodes with **zero** errors on
both constellation sizes, plus two narrow, explained failure points
(exactly 0.25 and 0.5) to avoid. This is still a lossless,
phase/timing-known loopback result, not a real-link one -- the next
blocker is unchanged: no carrier-phase recovery and no symbol-timing
recovery exist yet, both real prerequisites before any of this could
run against actual call audio, where the far end's carrier phase and
symbol clock are unknown and drifting rather than known exactly the way
this loopback assumes. Without at least a first cut at those two, even
clause 11.3's Phase 3 sequencing (PP/TRN/J/MD) has nothing reliable to
sequence over a real call, let alone the adaptive equalizer, shell
mapping, and trellis coding still further out. That is the recommended
next step.
