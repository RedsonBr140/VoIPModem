#include "FskModulator.h"

#include <cmath>

namespace voipmodem::dsp {

FskModulator::FskModulator(double markHz, double spaceHz, double baudRate, Sample amplitude, int sampleRateHz)
    : dds_(markHz, sampleRateHz),
      markHz_(markHz),
      spaceHz_(spaceHz),
      amplitude_(amplitude),
      exactSamplesPerBit_(static_cast<double>(sampleRateHz) / baudRate) {}

void FskModulator::ModulateBit(std::vector<Sample>& out, bool bit) {
    dds_.SetFrequency(bit ? markHz_ : spaceHz_);

    samplesPerBitRemainder_ += exactSamplesPerBit_;
    const int sampleCount = static_cast<int>(std::llround(samplesPerBitRemainder_));
    samplesPerBitRemainder_ -= sampleCount;

    out.reserve(out.size() + static_cast<size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i) {
        out.push_back(amplitude_ * dds_.NextSample());
    }
}

void FskModulator::ModulateByteAsync(std::vector<Sample>& out, std::uint8_t byte) {
    ModulateBit(out, false); // start bit: space/0
    for (int i = 0; i < 8; ++i) {
        ModulateBit(out, static_cast<bool>((byte >> i) & 0x01)); // LSB first
    }
    ModulateBit(out, true); // stop bit: mark/1
}

void FskModulator::Reset() {
    dds_.Reset();
    samplesPerBitRemainder_ = 0.0;
}

} // namespace voipmodem::dsp
