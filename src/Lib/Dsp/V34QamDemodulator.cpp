#include "V34QamDemodulator.h"

#include "V34QamModulator.h"

#include <algorithm>
#include <cmath>

namespace voipmodem::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// Fixed discrete-time FIR matched-filter taps, sampled ONCE at
// kSampleRateHz from the closed-form SRRC pulse -- the standard way to
// implement a symbol-rate pulse-shaping/matched filter in a system that
// runs at a fixed sample rate. taps[halfWidth] is the pulse's peak
// (t = 0); the array has 2*halfWidth + 1 entries.
std::vector<double> BuildMatchedFilterTaps(double symbolRateHz, double rolloffFactor, int filterSpanSymbols,
                                            int& halfWidthOut) {
    const double T = 1.0 / symbolRateHz;
    const double sampleRate = static_cast<double>(kSampleRateHz);
    const int halfWidth = static_cast<int>(std::llround(filterSpanSymbols * T * sampleRate));
    halfWidthOut = halfWidth;

    std::vector<double> taps(static_cast<size_t>(2 * halfWidth + 1));
    for (int j = -halfWidth; j <= halfWidth; ++j) {
        taps[static_cast<size_t>(j + halfWidth)] =
            SquareRootRaisedCosinePulse(static_cast<double>(j) / sampleRate, T, rolloffFactor);
    }
    return taps;
}

// Ordinary discrete convolution of `signal` with the (symmetric) FIR
// `taps`, producing an output the same length as `signal` ("same"
// convolution): output[n] = sum_m signal[n - m + halfWidth] * taps[m],
// for m spanning the tap array, with out-of-range signal samples
// treated as zero. Because SquareRootRaisedCosinePulse is an even
// function (SquareRootRaisedCosinePulse_IsSymmetric), convolving with
// these taps is equivalent to correlating with them -- this is the
// matched filter.
std::vector<double> Convolve(const std::vector<double>& signal, const std::vector<double>& taps, int halfWidth) {
    const auto n = static_cast<int>(signal.size());
    std::vector<double> output(static_cast<size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        double sum = 0.0;
        for (int m = -halfWidth; m <= halfWidth; ++m) {
            const int srcIndex = i - m;
            if (srcIndex < 0 || srcIndex >= n) {
                continue;
            }
            sum += signal[static_cast<size_t>(srcIndex)] * taps[static_cast<size_t>(m + halfWidth)];
        }
        output[static_cast<size_t>(i)] = sum;
    }
    return output;
}

// Lanczos (windowed-sinc) fractional-sample interpolator: reconstructs
// `signal`'s value at the real-valued `position` from its nearby
// integer samples. Valid for a signal that is itself comfortably
// band-limited within the Nyquist rate it was sampled at -- true here
// because `signal` is Convolve()'s output, i.e. already baseband-
// filtered to the RC pulse's own bandwidth, well inside kSampleRateHz's
// Nyquist limit (unlike the raw passband waveform, which is not safe to
// interpolate this way -- see the class comment on why the first
// version's approach of working with the raw waveform directly broke).
// `a` is the kernel half-width in samples (both the window radius and
// the number of sinc lobes kept on each side); larger is more accurate
// and more expensive. Positions outside [0, signal.size()) contribute
// nothing from the missing side.
double LanczosInterpolate(const std::vector<double>& signal, double position, int a) {
    const auto base = static_cast<long long>(std::floor(position));
    double sum = 0.0;
    for (long long j = base - a + 1; j <= base + a; ++j) {
        if (j < 0 || j >= static_cast<long long>(signal.size())) {
            continue;
        }
        const double x = position - static_cast<double>(j);
        double weight;
        if (std::abs(x) < 1e-9) {
            weight = 1.0;
        } else if (std::abs(x) >= a) {
            weight = 0.0;
        } else {
            weight = (std::sin(kPi * x) / (kPi * x)) * (std::sin(kPi * x / a) / (kPi * x / a));
        }
        sum += signal[static_cast<size_t>(j)] * weight;
    }
    return sum;
}

// Gardner timing-error detector gains, same PI structure (and same
// clamped, don't-let-one-bad-symbol-blow-up-the-loop philosophy) as
// FskDemodulator's own early-late gate -- see that class's
// kProportionalGain/kIntegralGain. Tuned empirically against a
// deliberately mistimed/mistuned loopback (see
// V34QamDemodulatorTests.cpp), not copied from FskDemodulator's values,
// since the error signal and symbol structure here are different (a
// non-data-aided Gardner TED over a QAM stream, not an FSK bit clock).
// Gains tuned to be small enough that the loop stays silent (no added
// decode errors) on an already-correctly-timed signal over a long
// burst, where the only input driving it is the Gardner TED's ordinary
// data-pattern-dependent noise, not a real error to correct -- a
// larger gain was measured to "lock" faster but then random-walk the
// position by several samples over the same burst, which is worse than
// doing nothing. See the class comment's "SYMBOL-TIMING RECOVERY"
// section for what this gain is and is not able to track, and why: a
// two-stage (fast-acquisition then narrow-tracking) gain schedule was
// tried and measured not to help, for a reason specific to this
// implementation, not a tuning failure -- see that comment.
constexpr double kGardnerProportionalGain = 0.0003;
constexpr double kGardnerIntegralGain = 0.00002;
constexpr double kMaxRateCorrectionFractionOfSymbol = 0.05;
constexpr double kMaxProportionalNudgeFractionOfSymbol = 0.25;
// The integral term is "leaky" (decayed slightly every symbol) rather
// than a pure accumulator. A plain accumulator driven by a
// non-data-aided TED's symbol-to-symbol noise (which is zero-MEAN at
// true lock, but has real, data-pattern-dependent variance) performs a
// random walk over a long burst with no restoring force -- measured
// directly: even with the waveform's actual symbol rate exactly
// matching what this object was told to expect (zero real drift to
// correct), a pure accumulator wandered by 2-3 samples after ~2000
// symbols. Decaying the integral state pulls it back toward zero
// whenever the input isn't a PERSISTENT bias, while still letting it
// settle at a nonzero steady state in response to a real, sustained
// rate offset (see Demodulate_ToleratesSlowSymbolClockDrift).
constexpr double kRateCorrectionDecayPerSymbol = 0.999;

// Carrier-phase recovery gains: a non-data-aided, 4th-power
// ("Costas-like") phase-error detector feeding a PI loop, same overall
// shape as the Gardner timing loop above (and FskDemodulator's
// early-late gate) but tracking rotation instead of sample position.
// Tuned the same way, against a deliberately phase-rotated loopback
// (see V34QamDemodulatorTests.cpp) -- not copied from the Gardner
// gains, since raising a complex sample to the 4th power scales its
// magnitude by the power squared rather than linearly, so the natural
// error-signal scale is very different.
// A two-stage (fast-acquire, then-narrow) schedule was tried here too
// (same idea as the Gardner loop's own, which didn't help ITS problem
// -- see that comment -- but this is a different loop/problem, so it
// was worth retesting on its own merits). Measured result: it did NOT
// help -- a much larger acquisition-stage gain still left the
// deliberately-misaligned test case barely improved within its
// acquisition window, while simultaneously breaking the
// already-correctly-phased tests the same way an over-large single
// gain did before. Kept as one small, steady-state-safe gain; see the
// class comment's "CARRIER-PHASE RECOVERY" section for what this
// actually achieves and doesn't.
constexpr double kPhaseProportionalGain = 0.001;
constexpr double kPhaseIntegralGain = 0.0001;
constexpr double kMaxFrequencyCorrectionRadiansPerSymbol = 0.2;
constexpr double kMaxPhaseNudgeRadians = 0.5;
// Same leaky-integrator reasoning as kRateCorrectionDecayPerSymbol
// above, same measured justification: a pure accumulator here also
// random-walked an already-correctly-phased signal over a long burst.
constexpr double kFrequencyCorrectionDecayPerSymbol = 0.999;

// The 4th-power detector's natural "zero error" reference point is NOT
// always 0 radians -- it depends on the constellation's own intrinsic
// angle. V34TrnGenerator's base point ("point 0" of the quarter
// superconstellation, V34Constellation.cpp's kQuarterConstellation[0])
// is exactly (1, 1), i.e. 45 degrees -- so for a correctly-phased
// signal, 4*(45 + 90*k) = 180 + 360*k degrees for every rotation k the
// scrambler happens to pick, a CONSTANT (data-independent, as
// expected) but equal to 180 degrees, not 0. Measured directly before
// this constant was accounted for: the raw detector output sat right
// on atan2's +/-180-degree branch cut, which is numerically exactly
// the wrong place to sit -- floating-point noise flips the sign
// essentially at random there, which is why early testing without this
// correction saw large, erratic, often-flipping error values even with
// zero real carrier-phase error. Pre-rotating by this known reference
// before the 4th power moves the expected lock point away from that
// cut entirely.
constexpr double kReferencePhaseRadians = kPi / 4.0;

} // namespace

V34QamDemodulator::V34QamDemodulator(double symbolRateHz, double carrierFrequencyHz, double rolloffFactor,
                                      int filterSpanSymbols)
    : symbolRateHz_(symbolRateHz),
      carrierFrequencyHz_(carrierFrequencyHz),
      rolloffFactor_(rolloffFactor),
      filterSpanSymbols_(filterSpanSymbols) {}

std::vector<std::pair<double, double>> V34QamDemodulator::Demodulate(const std::vector<Sample>& waveform,
                                                                        int numSymbols) const {
    std::vector<std::pair<double, double>> result;
    if (numSymbols <= 0) {
        return result;
    }
    result.reserve(static_cast<size_t>(numSymbols));

    const double T = 1.0 / symbolRateHz_;
    const double sampleRate = static_cast<double>(kSampleRateHz);
    const auto totalSamples = static_cast<int>(waveform.size());

    // Step 1: digital downconversion -- multiply the real passband
    // waveform by the same absolute carrier-phase reference
    // V34QamModulator::Modulate used, producing baseband-plus-image
    // I/Q streams at the full sample rate.
    std::vector<double> iRaw(static_cast<size_t>(totalSamples));
    std::vector<double> qRaw(static_cast<size_t>(totalSamples));
    for (int n = 0; n < totalSamples; ++n) {
        const double carrierPhase = kTwoPi * carrierFrequencyHz_ * (static_cast<double>(n) / sampleRate);
        const double x = static_cast<double>(waveform[static_cast<size_t>(n)]);
        iRaw[static_cast<size_t>(n)] = x * std::cos(carrierPhase);
        qRaw[static_cast<size_t>(n)] = -x * std::sin(carrierPhase);
    }

    // Step 2: a FIXED discrete-time matched filter, applied by ordinary
    // convolution -- this is what removes the downconversion's image at
    // 2x the carrier frequency and performs the actual pulse matching,
    // entirely at integer sample indices (no fractional evaluation
    // here, so no over-wide product can alias; see the class comment).
    int halfWidth = 0;
    const auto taps = BuildMatchedFilterTaps(symbolRateHz_, rolloffFactor_, filterSpanSymbols_, halfWidth);
    const auto iFiltered = Convolve(iRaw, taps, halfWidth);
    const auto qFiltered = Convolve(qRaw, taps, halfWidth);

    // Step 3: pick each symbol's value out of the now-baseband streams
    // via fractional-sample interpolation, tracked with a Gardner
    // timing loop rather than assuming the exact analytic symbol
    // position -- see the class comment's "SYMBOL-TIMING RECOVERY"
    // section. `symbolPositionSamples` starts at the same initial guess
    // the earlier, non-recovering version always used (sample 0 is
    // filterSpanSymbols_ symbol periods before symbol 0's center), but
    // is then adjusted every symbol by a PI loop driven by the
    // non-data-aided Gardner timing-error detector, so it can correct
    // both an imperfect initial guess and a persistent symbol-rate
    // mismatch between symbolRateHz_ (what this object was told to
    // expect) and the waveform's actual, possibly slightly different,
    // real symbol rate.
    constexpr int kLanczosA = 4;
    const double nominalSamplesPerSymbol = sampleRate * T;
    double symbolPositionSamples = filterSpanSymbols_ * nominalSamplesPerSymbol;
    double rateCorrection = 0.0;
    double runningPower = 1.0; // seeded away from 0 so early gain normalization isn't a divide-by-near-zero

    double previousI = LanczosInterpolate(iFiltered, symbolPositionSamples - nominalSamplesPerSymbol, kLanczosA);
    double previousQ = LanczosInterpolate(qFiltered, symbolPositionSamples - nominalSamplesPerSymbol, kLanczosA);

    // Carrier-phase recovery state -- see the class comment's
    // "CARRIER-PHASE RECOVERY" section. `carrierPhaseEstimate` is this
    // loop's running estimate of the constant-or-slowly-varying
    // rotation error left over from Step 1's downconversion (which has
    // no way to know the far end's true carrier phase); every symbol's
    // (currentI, currentQ) is de-rotated by it before being returned.
    double carrierPhaseEstimate = 0.0;
    double carrierFrequencyCorrection = 0.0;

    for (int k = 0; k < numSymbols; ++k) {
        const double currentI = LanczosInterpolate(iFiltered, symbolPositionSamples, kLanczosA);
        const double currentQ = LanczosInterpolate(qFiltered, symbolPositionSamples, kLanczosA);

        const double midPosition = symbolPositionSamples - nominalSamplesPerSymbol / 2.0;
        const double midI = LanczosInterpolate(iFiltered, midPosition, kLanczosA);
        const double midQ = LanczosInterpolate(qFiltered, midPosition, kLanczosA);

        // Non-data-aided Gardner timing-error detector: Re{y_mid *
        // conj(y_cur - y_prev)}. Positive when the current sampling
        // instant is late relative to the true symbol transition
        // nearest the midpoint, negative when early -- independent of
        // carrier phase and of what was actually transmitted, which is
        // why this works without a receive-side symbol decision.
        const double error = midI * (currentI - previousI) + midQ * (currentQ - previousQ);

        runningPower = 0.95 * runningPower + 0.05 * (currentI * currentI + currentQ * currentQ);
        const double normalizedError = error / std::max(runningPower, 1e-9);

        const double maxRateCorrection = kMaxRateCorrectionFractionOfSymbol * nominalSamplesPerSymbol;
        rateCorrection =
            std::clamp(kRateCorrectionDecayPerSymbol * rateCorrection + kGardnerIntegralGain * normalizedError,
                       -maxRateCorrection, maxRateCorrection);
        const double maxProportionalNudge = kMaxProportionalNudgeFractionOfSymbol * nominalSamplesPerSymbol;
        const double proportionalNudge =
            std::clamp(kGardnerProportionalGain * normalizedError, -maxProportionalNudge, maxProportionalNudge);

        previousI = currentI;
        previousQ = currentQ;
        symbolPositionSamples += nominalSamplesPerSymbol + rateCorrection + proportionalNudge;

        // De-rotate this symbol by the current carrier-phase estimate
        // (multiplying the complex sample by e^{-j*carrierPhaseEstimate})
        // *before* using it for anything else -- the 4th-power detector
        // below is computed from the already-corrected value, which is
        // what lets carrierPhaseEstimate converge (each update nudges
        // toward zero residual error on the corrected signal, not the
        // raw one).
        const double cosPhase = std::cos(carrierPhaseEstimate);
        const double sinPhase = std::sin(carrierPhaseEstimate);
        const double rotatedI = currentI * cosPhase + currentQ * sinPhase;
        const double rotatedQ = -currentI * sinPhase + currentQ * cosPhase;

        // Non-data-aided 4th-power phase-error detector: for any
        // constellation built, like V34TrnGenerator's, from a base
        // point rotated by k*90 degrees (clause 10.1.3.8's quarter
        // superconstellation), raising the de-rotated sample to the
        // 4th power removes that 4-fold rotational symmetry entirely
        // -- a correctly phased sample's 4th power lands at a FIXED
        // angle (not necessarily 0; see kReferencePhaseRadians above),
        // regardless of which of the 4 rotations the actual data
        // happened to use. First pre-rotate by -kReferencePhaseRadians
        // so that fixed angle becomes exactly 0 rather than landing on
        // atan2's +/-180-degree branch cut (see kReferencePhaseRadians's
        // own comment for why that matters numerically). atan2 of the
        // result is then 4x the residual phase error (mod 2*pi);
        // dividing by 4 recovers it, up to the inherent, unresolvable
        // +/-90-degree-multiple ambiguity every M-th-power carrier
        // recovery technique has (see the class comment -- fixing that
        // needs a differentially-encoded or known reference symbol,
        // neither of which exists here yet).
        const double refCos = std::cos(kReferencePhaseRadians);
        const double refSin = std::sin(kReferencePhaseRadians);
        const double refI = rotatedI * refCos + rotatedQ * refSin;
        const double refQ = -rotatedI * refSin + rotatedQ * refCos;
        const double y2Re = refI * refI - refQ * refQ;
        const double y2Im = 2.0 * refI * refQ;
        const double y4Re = y2Re * y2Re - y2Im * y2Im;
        const double y4Im = 2.0 * y2Re * y2Im;
        const double phaseError = std::atan2(y4Im, y4Re) / 4.0; // atan2 is already magnitude-independent

        carrierFrequencyCorrection =
            std::clamp(kFrequencyCorrectionDecayPerSymbol * carrierFrequencyCorrection + kPhaseIntegralGain * phaseError,
                       -kMaxFrequencyCorrectionRadiansPerSymbol, kMaxFrequencyCorrectionRadiansPerSymbol);
        const double phaseNudge = std::clamp(kPhaseProportionalGain * phaseError, -kMaxPhaseNudgeRadians,
                                              kMaxPhaseNudgeRadians);
        carrierPhaseEstimate += carrierFrequencyCorrection + phaseNudge;
        // Keep the running phase estimate bounded; it is used only
        // through cos/sin, which are periodic, so wrapping changes
        // nothing numerically.
        carrierPhaseEstimate = std::fmod(carrierPhaseEstimate, kTwoPi);

        result.emplace_back(rotatedI, rotatedQ);
    }

    return result;
}

} // namespace voipmodem::dsp
