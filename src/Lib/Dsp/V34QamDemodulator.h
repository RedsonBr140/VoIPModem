#pragma once

#include "DspTypes.h"

#include <utility>
#include <vector>

namespace voipmodem::dsp {

// The receive-side counterpart to V34QamModulator: carrier
// downconversion + SRRC matched filtering, sampled at the symbol rate,
// turning a passband waveform back into a sequence of (I, Q) estimates.
//
// IMPORTANT, read before trusting or extending this -- this is
// deliberately NOT a real V.34 receiver, for the same reason
// V34QamModulator is not yet wired into a real call: it does no
// carrier-phase recovery and no symbol-timing recovery. It assumes the
// caller already knows both exactly -- specifically, that sample 0 of
// `waveform` sits exactly `filterSpanSymbols` symbol periods before
// symbol 0's center, the same convention V34QamModulator::Modulate
// uses for its own output. That is true in a lossless loopback test
// (modulate, then immediately demodulate the exact same buffer) but
// would never be true on a real link, where the far end's carrier
// phase and symbol clock are unknown and drifting. A real V.34 receiver
// needs its own carrier recovery (akin to what V8Handshake eventually
// needed for FSK, clause 9.6) and timing recovery (akin to
// FskDemodulator's early-late gate, clause 9.5/equalizer) before this
// class's approach could run on real call audio -- neither exists yet.
//
// What this DOES verify, and why that is still worth having before any
// of that exists: that the downconversion + SRRC-matched-filter
// arithmetic itself is correct -- i.e. that V34QamModulator's output,
// fed through the textbook-matched receive filter, actually recovers
// the original symbol sequence (up to one unknown real-valued scale
// factor; see Demodulate()'s own comment for why no phase ambiguity
// also arises here). That is a meaningful, independently-testable
// building block the same way Dds/GoertzelDetector were verified in
// isolation before FskModulator/FskDemodulator combined them, and the
// same way V8Handshake's own loopback test (TX fed directly into RX,
// no real audio path) was run before any live-call test was attempted.
//
// FIRST VERSION'S FINDING, FIXED BELOW -- kept here because it explains
// why this class is built the way it is, not the naive way: a first
// implementation evaluated the matched filter as
// sum_n waveform[n] * h(t_n - t_k) directly, per symbol k, using the
// closed-form SRRC formula at each symbol's own (generally fractional)
// real-valued offset. That produced severe residual ISI at V.34's
// actual 2400 baud / 8 kHz rate (~3.33 samples/symbol) -- confirmed via
// measurement to be neither a logic bug nor an aliasing problem (the
// signal itself sits well inside the 8 kHz Nyquist bandwidth), but a
// genuine quadrature artifact: the quantity actually being summed,
// waveform(t)*h(t-t_k), is a PRODUCT of two band-limited signals (the
// received passband signal and the shifted pulse), and such a product
// can occupy close to the SUM of their two bandwidths -- which, worked
// out numerically, exceeds the 8 kHz Nyquist limit here even though
// each factor alone does not. A per-symbol Riemann sum of real samples
// against that over-wide product has no reason to converge, and
// measurably didn't (~76% of symbols decoded wrong on a real 16-point
// TRN burst; a much wider filterSpanSymbols barely helped, which is
// consistent with this being a quadrature problem rather than a
// truncation one).
//
// This version avoids that failure mode by never forming that product
// at a per-symbol level at all. It separates the two operations a real
// fractionally-spaced receiver keeps separate:
//   1. Digital downconversion, then an ordinary FIXED discrete-time FIR
//      filter (matched to the SRRC pulse, taps sampled once at
//      kSampleRateHz) applied by normal convolution across every sample
//      -- producing continuous, genuinely baseband (bandwidth limited
//      to the RC mask, comfortably inside 8 kHz Nyquist on its own) I/Q
//      streams at the full 8 kHz rate. No fractional offsets appear
//      anywhere in this step, so there is no over-wide product to
//      alias.
//   2. A separate windowed-sinc (Lanczos) fractional-sample
//      interpolator picks the symbol-rate value out of those already-
//      baseband streams at each symbol's (generally non-integer)
//      sample position. Interpolating a signal that is itself safely
//      within Nyquist is a well-conditioned problem, unlike evaluating
//      the earlier per-symbol product directly.
//
// This is still NOT a real V.34 receiver, for the same reason
// V34QamModulator is not yet wired into a real call: it still does no
// carrier-phase recovery at all (see "SYMBOL-TIMING RECOVERY" below for
// the symbol-timing side, which this version now has in a limited
// form). See V34QamDemodulatorTests.cpp for what this version was
// actually measured to achieve at V.34's real parameters versus the
// broken first attempt above.
//
// MEASURED RESULT, this version, at V.34's actual 2400 baud / 8 kHz /
// 1800 Hz carrier: with the same rolloff (0.125) the modulator's own
// historical tests use, residual ISI is real but much smaller than the
// first version's (~8.5% of symbols wrong on a 1000-symbol 4-point
// burst, versus ~76% before). With rolloff raised to 0.3 -- still a
// free implementation choice, not something clause 5.4.1 mandates --
// it decodes PERFECTLY (0 symbol errors) on both a 4-point and a
// 16-point burst at this exact symbol rate/carrier/sample rate. A
// rolloff sweep found a wide, robust working range (~0.28-0.48) with
// two narrow, explainable failure points at exactly 0.25 and 0.5 (the
// RC bandwidth landing where the downconversion image aliases back
// into the wanted band) -- stay away from those two exact values, and
// 0.3 is a safe, tested choice well clear of both. Any future code that
// wires this demodulator into a real V.34 Phase 3 signal at this
// symbol rate should use that rolloff (and the matching TX rolloff on
// V34QamModulator -- matched filtering requires both sides to agree),
// not the 0.125 default.
//
// SYMBOL-TIMING RECOVERY (added after the above, same investigation):
// Demodulate() no longer assumes the exact analytic symbol position --
// it tracks it with a non-data-aided Gardner timing-error detector
// feeding a PI loop (same proportional+integral structure as
// FskDemodulator's early-late gate, different error signal). Measured,
// not assumed: this tolerates a REALISTIC symbol-clock mismatch between
// what this object is told to expect and the waveform's actual rate --
// tested up to +/-100 ppm (a generous real crystal-oscillator
// tolerance) over an 800-symbol burst with zero added decode errors.
// It does NOT, however, meaningfully fix not knowing where symbol 0
// actually starts: a deliberate 1-sample shift of the whole waveform
// still fails badly, for a reason specific to this implementation, not
// a tuning shortfall -- shifting the raw waveform also rotates the
// carrier phase used by Demodulate()'s own downconversion step (which
// is referenced to each sample's absolute index), and with no
// carrier-phase recovery anywhere in this codebase, that phase error
// alone is enough to break decisions regardless of timing. In other
// words: symbol-timing recovery and carrier-phase recovery are coupled
// in this design, not independent problems that can be solved one at a
// time against a realistic unknown-start-time scenario. See
// V34QamDemodulatorTests.cpp's
// Demodulate_SampleAlignmentShift_CarrierPhaseLoopImprovesButDoesNotConverge_KnownLimitation
// for the measurement (updated below, since carrier-phase recovery now
// exists), and do not attempt to fix that test by tuning the Gardner
// loop further -- a two-stage (fast-acquisition, then-narrow) gain
// schedule was tried and did not help, because the problem there is
// phase, not timing bandwidth.
//
// CARRIER-PHASE RECOVERY (added after the above, same investigation):
// Demodulate() now also runs a non-data-aided, 4th-power
// ("Costas-like") phase-error detector feeding its own PI loop (same
// shape as the Gardner loop above), de-rotating every symbol by the
// running phase estimate before returning it. Getting this right
// needed a real, non-obvious correction, found by measurement, not
// guessed: the 4th-power detector's "zero error" reference angle is
// NOT always 0 -- it depends on the constellation's own intrinsic
// angle. V34TrnGenerator's base point (kQuarterConstellation[0]) is
// exactly (1, 1), i.e. 45 degrees, so a correctly-phased signal's 4th
// power sits at a CONSTANT 180 degrees, not 0 -- which, left
// uncorrected, put the raw detector output right on atan2's
// +/-180-degree branch cut, flipping sign essentially at random from
// floating-point noise alone. The fix: pre-rotate by this known
// reference (kReferencePhaseRadians in the .cpp) before the 4th power,
// moving the true lock point away from that cut. Without this fix the
// loop was unusable -- large, erratic, sign-flipping error even with
// zero real phase error to correct.
// With that fixed and gains tuned the same way as Gardner's (small
// enough to stay silent on an already-correctly-phased signal; a
// two-stage schedule was tried here too and, unlike for Gardner, still
// didn't help -- see kPhaseProportionalGain's own comment), this loop:
// stays silent (no added decode errors) on every already-correct-phase
// test in this file; and, measured on the 1-sample-shift scenario
// above, genuinely and monotonically reduces decode errors over a long
// (1500-symbol) burst (~95% wrong in the first third, ~53% wrong in
// the last) -- real progress, not nothing. But it does NOT converge
// fast enough to actually fix that scenario within a realistic burst
// length. Symbol timing and carrier phase are no longer "blocked on
// each other" in the sense of one not existing at all, but a
// large/sudden initial misalignment in either is still not something
// either loop, as currently tuned, can acquire quickly -- both are
// built for already-close tracking, not blind acquisition. A faster,
// separate acquisition mechanism (e.g. a short, known preamble searched
// for at startup, the way V8Handshake's own sync gate works, rather
// than relying on these two tracking loops to pull in a large error
// from a cold start) is the realistic next step, not further gain
// tuning on either loop.
class V34QamDemodulator {
public:
    // Parameters must match the V34QamModulator that produced
    // `waveform` exactly (symbol rate, carrier frequency, roll-off,
    // filter span) -- this class does not infer any of them.
    V34QamDemodulator(double symbolRateHz, double carrierFrequencyHz, double rolloffFactor = 0.125,
                       int filterSpanSymbols = 8);

    // Recovers `numSymbols` (I, Q) estimates from `waveform`, assuming
    // the same sample-0-to-symbol-0 timing and absolute carrier-phase
    // reference V34QamModulator::Modulate used to produce it (see class
    // comment). The returned values are proportional to the original
    // transmit constellation points but at an unknown real-valued scale
    // (a function of the transmit amplitude, the carrier downconversion,
    // and the matched filter's self-convolution energy) -- callers that
    // need actual constellation decisions must estimate that scale
    // themselves (e.g. via a handful of known training symbols), the
    // same way a real equalizer would from TRN. No phase/quadrature
    // ambiguity is introduced by this class itself, because it reuses
    // the exact same absolute carrier-phase reference
    // (cos/sin(2*pi*carrierFrequencyHz*n/kSampleRateHz) for sample
    // index n) that Modulate() used -- a real receiver would not have
    // this for free.
    std::vector<std::pair<double, double>> Demodulate(const std::vector<Sample>& waveform, int numSymbols) const;

private:
    double symbolRateHz_;
    double carrierFrequencyHz_;
    double rolloffFactor_;
    int filterSpanSymbols_;
};

} // namespace voipmodem::dsp
