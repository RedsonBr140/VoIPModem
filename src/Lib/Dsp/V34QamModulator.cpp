#include "V34QamModulator.h"

#include <algorithm>
#include <cmath>

namespace voipmodem::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// Raw (unnormalized) closed-form square-root-raised-cosine impulse
// response, standard textbook formula, with the two removable
// singularities (t = 0 and t = +/- T/(4*beta)) handled explicitly
// rather than relying on floating-point division to happen to produce
// the right limit.
double RawSrrc(double t, double T, double beta) {
    const double eps = T * 1e-9;
    if (std::abs(t) < eps) {
        return (1.0 / std::sqrt(T)) * (1.0 - beta + 4.0 * beta / kPi);
    }
    const double quarterPoint = T / (4.0 * beta);
    if (std::abs(std::abs(t) - quarterPoint) < eps) {
        const double arg = kPi / (4.0 * beta);
        return (beta / (T * std::sqrt(2.0))) *
               ((1.0 + 2.0 / kPi) * std::sin(arg) + (1.0 - 2.0 / kPi) * std::cos(arg));
    }
    const double numerator = std::sin(kPi * t * (1.0 - beta) / T) + 4.0 * beta * t / T * std::cos(kPi * t * (1.0 + beta) / T);
    const double ratio = 4.0 * beta * t / T;
    const double denominator = kPi * t / T * (1.0 - ratio * ratio);
    return (1.0 / std::sqrt(T)) * numerator / denominator;
}
} // namespace

double SquareRootRaisedCosinePulse(double t, double symbolPeriodSeconds, double beta) {
    const double peak = RawSrrc(0.0, symbolPeriodSeconds, beta);
    return RawSrrc(t, symbolPeriodSeconds, beta) / peak;
}

V34QamModulator::V34QamModulator(double symbolRateHz, double carrierFrequencyHz, double rolloffFactor,
                                  int filterSpanSymbols)
    : symbolRateHz_(symbolRateHz),
      carrierFrequencyHz_(carrierFrequencyHz),
      rolloffFactor_(rolloffFactor),
      filterSpanSymbols_(filterSpanSymbols) {}

std::vector<Sample> V34QamModulator::Modulate(const std::vector<std::pair<int, int>>& symbols,
                                               double amplitude) const {
    if (symbols.empty()) {
        return {};
    }

    const double T = 1.0 / symbolRateHz_;
    const double sampleRate = static_cast<double>(kSampleRateHz);
    const int numSymbols = static_cast<int>(symbols.size());

    // Pad filterSpanSymbols_ symbol periods of filter tail on each
    // side, so the pulse for the first/last real symbols is not
    // truncated at the buffer edge.
    const double totalDurationSeconds = (numSymbols + 2.0 * filterSpanSymbols_) * T;
    const auto totalSamples = static_cast<size_t>(std::llround(totalDurationSeconds * sampleRate));

    std::vector<Sample> output(totalSamples, 0.0f);

    for (size_t n = 0; n < totalSamples; ++n) {
        // Shift time so sample 0 sits filterSpanSymbols_ symbol
        // periods before symbol 0's center (t = 0).
        const double t = (static_cast<double>(n) / sampleRate) - filterSpanSymbols_ * T;

        const int centerSymbol = static_cast<int>(std::floor(t / T + 0.5));
        const int lo = std::max(0, centerSymbol - filterSpanSymbols_);
        const int hi = std::min(numSymbols - 1, centerSymbol + filterSpanSymbols_);

        double iSum = 0.0;
        double qSum = 0.0;
        for (int k = lo; k <= hi; ++k) {
            const double tk = t - k * T;
            const double h = SquareRootRaisedCosinePulse(tk, T, rolloffFactor_);
            iSum += symbols[static_cast<size_t>(k)].first * h;
            qSum += symbols[static_cast<size_t>(k)].second * h;
        }

        const double carrierPhase = kTwoPi * carrierFrequencyHz_ * (static_cast<double>(n) / sampleRate);
        const double passband = amplitude * (iSum * std::cos(carrierPhase) - qSum * std::sin(carrierPhase));
        output[n] = static_cast<Sample>(passband);
    }

    return output;
}

} // namespace voipmodem::dsp
