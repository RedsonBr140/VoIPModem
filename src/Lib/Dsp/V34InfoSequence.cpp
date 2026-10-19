#include "V34InfoSequence.h"

#include "DbpskModulator.h"
#include "V8Signal.h"

namespace voipmodem::dsp {

namespace {
constexpr Sample kInfoAmplitude = 0.8f;

// Table 14's frame-sync pattern, bits 4:11, left-most bit first in
// time -- duplicated from V34Info0.cpp's own encode logic rather than
// exposing it there, since StreamingInfo0Receiver's search is this
// module's own concern, not V34Info0's bit-layout one.
const std::vector<bool> kFrameSyncPattern = {false, true, true, true, false, false, true, false};
constexpr size_t kFrameSyncBitOffset = 4; // where it sits inside the 49-bit frame
constexpr size_t kInfo0FrameLength = 49;
constexpr size_t kBoundedGrowthLimit = 256; // see V8Signal.h's FindBitPattern comment
} // namespace

void EncodeInfo0ToSamples(const V34Info0& info, double carrierHz, std::vector<Sample>& out, int sampleRateHz) {
    const std::vector<bool> bits = EncodeInfo0(info);

    DbpskModulator modulator(carrierHz, kInfoBaudRate, kInfoAmplitude, sampleRateHz);
    modulator.EmitReferenceSymbol(out);
    for (bool bit : bits) {
        modulator.ModulateBit(out, bit);
    }
}

std::optional<V34Info0> DecodeInfo0FromSamples(const std::vector<Sample>& samples, double carrierHz,
                                                int sampleRateHz) {
    DbpskDemodulator demodulator(carrierHz, kInfoBaudRate, sampleRateHz);
    const std::vector<bool> bits = demodulator.ProcessSamples(samples);
    return DecodeInfo0(bits);
}

StreamingInfo0Receiver::StreamingInfo0Receiver(double carrierHz, int sampleRateHz)
    : demodulator_(carrierHz, kInfoBaudRate, sampleRateHz) {}

std::optional<V34Info0> StreamingInfo0Receiver::ProcessSamples(const std::vector<Sample>& samples) {
    const std::vector<bool> newBits = demodulator_.ProcessSamples(samples);
    bits_.insert(bits_.end(), newBits.begin(), newBits.end());

    for (;;) {
        const std::optional<size_t> syncIndex = FindBitPattern(bits_, kFrameSyncPattern, searchResumeIndex_);
        if (!syncIndex.has_value()) {
            break;
        }
        if (*syncIndex < kFrameSyncBitOffset) {
            searchResumeIndex_ = *syncIndex + 1;
            continue;
        }
        const size_t frameStart = *syncIndex - kFrameSyncBitOffset;
        if (bits_.size() < frameStart + kInfo0FrameLength) {
            break; // not enough bits buffered yet to try this candidate
        }
        const std::vector<bool> candidate(bits_.begin() + static_cast<long>(frameStart),
                                           bits_.begin() + static_cast<long>(frameStart + kInfo0FrameLength));
        if (const std::optional<V34Info0> decoded = DecodeInfo0(candidate)) {
            bits_.erase(bits_.begin(), bits_.begin() + static_cast<long>(frameStart + kInfo0FrameLength));
            searchResumeIndex_ = 0;
            return decoded;
        }
        searchResumeIndex_ = *syncIndex + 1; // false positive on data bits -- keep searching
    }

    if (searchResumeIndex_ > kBoundedGrowthLimit) {
        bits_.erase(bits_.begin(), bits_.begin() + static_cast<long>(searchResumeIndex_));
        searchResumeIndex_ = 0;
    }

    return std::nullopt;
}

} // namespace voipmodem::dsp
