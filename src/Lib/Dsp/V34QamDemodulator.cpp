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
    // via fractional-sample interpolation. Same timing convention as
    // V34QamModulator::Modulate: sample 0 is filterSpanSymbols_ symbol
    // periods before symbol 0's center, so symbol k's center sample
    // position is at t = (k + filterSpanSymbols_) * T.
    constexpr int kLanczosA = 4;
    for (int k = 0; k < numSymbols; ++k) {
        const double tk = (k + filterSpanSymbols_) * T;
        const double positionSamples = tk * sampleRate;
        const double iHat = LanczosInterpolate(iFiltered, positionSamples, kLanczosA);
        const double qHat = LanczosInterpolate(qFiltered, positionSamples, kLanczosA);
        result.emplace_back(iHat, qHat);
    }

    return result;
}

} // namespace voipmodem::dsp
