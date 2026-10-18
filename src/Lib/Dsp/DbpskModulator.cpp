#include "DbpskModulator.h"

#include <cmath>

namespace voipmodem::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

DbpskModulator::DbpskModulator(double carrierHz, double baudRate, Sample amplitude, int sampleRateHz)
    : dds_(carrierHz, sampleRateHz),
      amplitude_(amplitude),
      exactSamplesPerSymbol_(static_cast<double>(sampleRateHz) / baudRate) {}

void DbpskModulator::EmitSymbol(std::vector<Sample>& out, bool rotate) {
    if (rotate) {
        dds_.AddPhase(kPi); // 180-degree jump, continuing from the previous symbol's phase
    }

    samplesPerSymbolRemainder_ += exactSamplesPerSymbol_;
    const int sampleCount = static_cast<int>(std::llround(samplesPerSymbolRemainder_));
    samplesPerSymbolRemainder_ -= sampleCount;

    out.reserve(out.size() + static_cast<size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i) {
        out.push_back(amplitude_ * dds_.NextSample());
    }
}

void DbpskModulator::ModulateBit(std::vector<Sample>& out, bool bit) {
    EmitSymbol(out, bit);
}

void DbpskModulator::EmitReferenceSymbol(std::vector<Sample>& out) {
    EmitSymbol(out, false);
}

void DbpskModulator::Reset() {
    dds_.Reset();
    samplesPerSymbolRemainder_ = 0.0;
}

} // namespace voipmodem::dsp
