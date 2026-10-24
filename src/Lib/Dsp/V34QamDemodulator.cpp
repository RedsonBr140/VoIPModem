#include "V34QamDemodulator.h"

#include "V34QamModulator.h"

#include <algorithm>
#include <cmath>

namespace voipmodem::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
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

    // Same timing convention as V34QamModulator::Modulate: sample 0 is
    // filterSpanSymbols_ symbol periods before symbol 0's center, so
    // symbol k's center sample index is at t = (k + filterSpanSymbols_) * T.
    for (int k = 0; k < numSymbols; ++k) {
        const double tk = (k + filterSpanSymbols_) * T;
        const auto centerSample = static_cast<int>(std::llround(tk * sampleRate));

        const auto halfWindowSamples =
            static_cast<int>(std::llround(filterSpanSymbols_ * T * sampleRate));
        const int lo = std::max(0, centerSample - halfWindowSamples);
        const int hi = std::min(totalSamples - 1, centerSample + halfWindowSamples);

        double iSum = 0.0;
        double qSum = 0.0;
        for (int n = lo; n <= hi; ++n) {
            const double tn = static_cast<double>(n) / sampleRate;
            const double carrierPhase = kTwoPi * carrierFrequencyHz_ * tn;
            const double h = SquareRootRaisedCosinePulse(tn - tk, T, rolloffFactor_);

            iSum += static_cast<double>(waveform[static_cast<size_t>(n)]) * std::cos(carrierPhase) * h;
            qSum += -static_cast<double>(waveform[static_cast<size_t>(n)]) * std::sin(carrierPhase) * h;
        }

        result.emplace_back(iSum, qSum);
    }

    return result;
}

} // namespace voipmodem::dsp
