#include "V34InfoSequence.h"

#include "DbpskModulator.h"
#include "V8Signal.h"

namespace voipmodem::dsp {

namespace {
constexpr Sample kInfoAmplitude = 0.8f;

// Tables 14/15/16's shared frame-sync pattern, bits 4:11, left-most
// bit first in time -- duplicated from V34Info0/V34Info1's own encode
// logic rather than exposing it there, since this search is this
// module's own concern, not the bit-layout modules'.
const std::vector<bool> kFrameSyncPattern = {false, true, true, true, false, false, true, false};
constexpr size_t kFrameSyncBitOffset = 4; // where it sits inside every INFO0/INFO1 frame
constexpr size_t kInfo0FrameLength = 49;
constexpr size_t kInfo1cFrameLength = 109;
constexpr size_t kInfo1aFrameLength = 70;
constexpr size_t kBoundedGrowthLimit = 256; // see V8Signal.h's FindBitPattern comment

// Shared search loop behind every Streaming*Receiver: searches `bits`
// (appending `newBits` first) for `frameLength`'s frame-sync pattern,
// tries `decode` on each candidate, and applies the same
// monotonically-advancing-resume-point/bounded-growth discipline as
// StreamingInfo0Receiver always has. `decode` takes exactly
// `frameLength` bits and returns the same optional<T> this returns.
template <typename T, typename DecodeFn>
std::optional<T> FindAndDecodeFrame(std::vector<bool>& bits, size_t& searchResumeIndex, size_t frameLength,
                                     DecodeFn decode) {
    for (;;) {
        const std::optional<size_t> syncIndex = FindBitPattern(bits, kFrameSyncPattern, searchResumeIndex);
        if (!syncIndex.has_value()) {
            break;
        }
        if (*syncIndex < kFrameSyncBitOffset) {
            searchResumeIndex = *syncIndex + 1;
            continue;
        }
        const size_t frameStart = *syncIndex - kFrameSyncBitOffset;
        if (bits.size() < frameStart + frameLength) {
            break; // not enough bits buffered yet to try this candidate
        }
        const std::vector<bool> candidate(bits.begin() + static_cast<long>(frameStart),
                                           bits.begin() + static_cast<long>(frameStart + frameLength));
        if (const std::optional<T> decoded = decode(candidate)) {
            bits.erase(bits.begin(), bits.begin() + static_cast<long>(frameStart + frameLength));
            searchResumeIndex = 0;
            return decoded;
        }
        searchResumeIndex = *syncIndex + 1; // false positive on data bits -- keep searching
    }

    if (searchResumeIndex > kBoundedGrowthLimit) {
        bits.erase(bits.begin(), bits.begin() + static_cast<long>(searchResumeIndex));
        searchResumeIndex = 0;
    }
    return std::nullopt;
}
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
    return FindAndDecodeFrame<V34Info0>(bits_, searchResumeIndex_, kInfo0FrameLength, DecodeInfo0);
}

void EncodeInfo1cToSamples(const V34Info1c& info, double carrierHz, std::vector<Sample>& out, int sampleRateHz) {
    const std::vector<bool> bits = EncodeInfo1c(info);
    DbpskModulator modulator(carrierHz, kInfoBaudRate, kInfoAmplitude, sampleRateHz);
    modulator.EmitReferenceSymbol(out);
    for (bool bit : bits) {
        modulator.ModulateBit(out, bit);
    }
}

void EncodeInfo1aToSamples(const V34Info1a& info, double carrierHz, std::vector<Sample>& out, int sampleRateHz) {
    const std::vector<bool> bits = EncodeInfo1a(info);
    DbpskModulator modulator(carrierHz, kInfoBaudRate, kInfoAmplitude, sampleRateHz);
    modulator.EmitReferenceSymbol(out);
    for (bool bit : bits) {
        modulator.ModulateBit(out, bit);
    }
}

StreamingInfo1cReceiver::StreamingInfo1cReceiver(double carrierHz, int sampleRateHz)
    : demodulator_(carrierHz, kInfoBaudRate, sampleRateHz) {}

std::optional<V34Info1c> StreamingInfo1cReceiver::ProcessSamples(const std::vector<Sample>& samples) {
    const std::vector<bool> newBits = demodulator_.ProcessSamples(samples);
    bits_.insert(bits_.end(), newBits.begin(), newBits.end());
    return FindAndDecodeFrame<V34Info1c>(bits_, searchResumeIndex_, kInfo1cFrameLength, DecodeInfo1c);
}

StreamingInfo1aReceiver::StreamingInfo1aReceiver(double carrierHz, int sampleRateHz)
    : demodulator_(carrierHz, kInfoBaudRate, sampleRateHz) {}

std::optional<V34Info1a> StreamingInfo1aReceiver::ProcessSamples(const std::vector<Sample>& samples) {
    const std::vector<bool> newBits = demodulator_.ProcessSamples(samples);
    bits_.insert(bits_.end(), newBits.begin(), newBits.end());
    return FindAndDecodeFrame<V34Info1a>(bits_, searchResumeIndex_, kInfo1aFrameLength, DecodeInfo1a);
}

} // namespace voipmodem::dsp
