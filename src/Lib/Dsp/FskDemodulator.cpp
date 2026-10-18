#include "FskDemodulator.h"

#include <cmath>

namespace voipmodem::dsp {

FskDemodulator::FskDemodulator(double markHz, double spaceHz, double baudRate, int sampleRateHz)
    : exactSamplesPerBit_(static_cast<double>(sampleRateHz) / baudRate),
      markDetector_(markHz, sampleRateHz),
      spaceDetector_(spaceHz, sampleRateHz) {}

std::vector<bool> FskDemodulator::ProcessSamples(const std::vector<Sample>& samples) {
    std::vector<bool> bits;

    for (Sample sample : samples) {
        if (currentBitTargetLength_ < 0) {
            bitLengthRemainder_ += exactSamplesPerBit_;
            currentBitTargetLength_ = static_cast<int>(std::llround(bitLengthRemainder_));
            bitLengthRemainder_ -= currentBitTargetLength_;
            markDetector_.Reset();
            spaceDetector_.Reset();
            samplesIntoCurrentBit_ = 0;
        }

        markDetector_.Process(sample);
        spaceDetector_.Process(sample);
        ++samplesIntoCurrentBit_;

        if (samplesIntoCurrentBit_ >= currentBitTargetLength_) {
            bits.push_back(markDetector_.Magnitude(currentBitTargetLength_) >
                            spaceDetector_.Magnitude(currentBitTargetLength_));
            currentBitTargetLength_ = -1; // next sample starts a new bit
        }
    }

    return bits;
}

std::optional<std::uint8_t> FskDemodulator::TryDecodeAsyncByte(const std::vector<bool>& bits, size_t& startIndex) {
    constexpr size_t kFrameBits = 10; // 1 start + 8 data + 1 stop
    if (startIndex + kFrameBits > bits.size()) {
        return std::nullopt;
    }

    const bool startBit = bits[startIndex];
    const bool stopBit = bits[startIndex + 9];
    if (startBit != false || stopBit != true) {
        return std::nullopt; // not actually framed here -- caller wasn't aligned, not a corrupt byte
    }

    std::uint8_t byte = 0;
    for (int i = 0; i < 8; ++i) {
        if (bits[startIndex + 1 + i]) {
            byte |= static_cast<std::uint8_t>(1u << i); // LSB first
        }
    }

    startIndex += kFrameBits;
    return byte;
}

} // namespace voipmodem::dsp
