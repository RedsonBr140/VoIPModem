#include "V34Info1.h"

#include "V34Crc.h"

namespace voipmodem::dsp {

namespace {

constexpr bool kFrameSync[8] = {false, true, true, true, false, false, true, false}; // 01110010

void AppendBitsLsbFirst(std::vector<bool>& bits, std::uint32_t value, int bitCount) {
    for (int i = 0; i < bitCount; ++i) {
        bits.push_back(((value >> i) & 0x01u) != 0);
    }
}

std::uint32_t ReadBitsLsbFirst(const std::vector<bool>& bits, size_t startIndex, int bitCount) {
    std::uint32_t value = 0;
    for (int i = 0; i < bitCount; ++i) {
        if (bits[startIndex + static_cast<size_t>(i)]) {
            value |= (1u << i);
        }
    }
    return value;
}

void AppendSignedLsbFirst(std::vector<bool>& bits, std::int16_t value, int bitCount) {
    AppendBitsLsbFirst(bits, static_cast<std::uint32_t>(value) & ((1u << bitCount) - 1u), bitCount);
}

std::int16_t ReadSignedLsbFirst(const std::vector<bool>& bits, size_t startIndex, int bitCount) {
    const std::uint32_t raw = ReadBitsLsbFirst(bits, startIndex, bitCount);
    const std::uint32_t signBit = 1u << (bitCount - 1);
    if ((raw & signBit) != 0) {
        return static_cast<std::int16_t>(static_cast<std::int32_t>(raw) - static_cast<std::int32_t>(1u << bitCount));
    }
    return static_cast<std::int16_t>(raw);
}

void AppendProbingResult(std::vector<bool>& bits, const V34ProbingResult& result) {
    bits.push_back(result.highCarrier);
    AppendBitsLsbFirst(bits, result.preEmphasisFilter, 4);
    AppendBitsLsbFirst(bits, result.projectedDataRateMultiplier, 4);
}

V34ProbingResult ReadProbingResult(const std::vector<bool>& bits, size_t startIndex) {
    V34ProbingResult result;
    result.highCarrier = bits[startIndex];
    result.preEmphasisFilter = static_cast<std::uint8_t>(ReadBitsLsbFirst(bits, startIndex + 1, 4));
    result.projectedDataRateMultiplier = static_cast<std::uint8_t>(ReadBitsLsbFirst(bits, startIndex + 5, 4));
    return result;
}

} // namespace

V34Info1c MakeDefaultLocalInfo1c() { return V34Info1c{}; }
V34Info1a MakeDefaultLocalInfo1a() { return V34Info1a{}; }

std::vector<bool> EncodeInfo1c(const V34Info1c& info) {
    std::vector<bool> bits;
    bits.reserve(109);
    bits.insert(bits.end(), {true, true, true, true});
    bits.insert(bits.end(), kFrameSync, kFrameSync + 8);

    std::vector<bool> payload;
    payload.reserve(77);
    AppendBitsLsbFirst(payload, info.minPowerReductionDb, 3);
    AppendBitsLsbFirst(payload, info.additionalPowerReductionDb, 3);
    AppendBitsLsbFirst(payload, info.mdLengthIn35msUnits, 7);
    AppendProbingResult(payload, info.rate2400);
    AppendProbingResult(payload, info.rate2743);
    AppendProbingResult(payload, info.rate2800);
    AppendProbingResult(payload, info.rate3000);
    AppendProbingResult(payload, info.rate3200);
    AppendProbingResult(payload, info.rate3429);
    AppendSignedLsbFirst(payload, info.frequencyOffsetRaw, 10);

    bits.insert(bits.end(), payload.begin(), payload.end());

    const std::uint16_t crc = ComputeV34Crc16(payload);
    AppendBitsLsbFirst(bits, crc, 16);

    bits.insert(bits.end(), {true, true, true, true});
    return bits;
}

std::optional<V34Info1c> DecodeInfo1c(const std::vector<bool>& bits) {
    constexpr size_t kTotalBits = 109;
    constexpr size_t kPayloadStart = 12;
    constexpr size_t kPayloadBits = 77;
    constexpr size_t kCrcStart = 89;

    if (bits.size() != kTotalBits) {
        return std::nullopt;
    }
    for (size_t i = 0; i < 8; ++i) {
        if (bits[4 + i] != kFrameSync[i]) {
            return std::nullopt;
        }
    }

    const std::vector<bool> payload(bits.begin() + static_cast<long>(kPayloadStart),
                                     bits.begin() + static_cast<long>(kPayloadStart + kPayloadBits));
    const std::uint16_t receivedCrc = static_cast<std::uint16_t>(ReadBitsLsbFirst(bits, kCrcStart, 16));
    if (receivedCrc != ComputeV34Crc16(payload)) {
        return std::nullopt;
    }

    V34Info1c info;
    info.minPowerReductionDb = static_cast<std::uint8_t>(ReadBitsLsbFirst(payload, 0, 3));
    info.additionalPowerReductionDb = static_cast<std::uint8_t>(ReadBitsLsbFirst(payload, 3, 3));
    info.mdLengthIn35msUnits = static_cast<std::uint8_t>(ReadBitsLsbFirst(payload, 6, 7));
    info.rate2400 = ReadProbingResult(payload, 13);
    info.rate2743 = ReadProbingResult(payload, 22);
    info.rate2800 = ReadProbingResult(payload, 31);
    info.rate3000 = ReadProbingResult(payload, 40);
    info.rate3200 = ReadProbingResult(payload, 49);
    info.rate3429 = ReadProbingResult(payload, 58);
    info.frequencyOffsetRaw = ReadSignedLsbFirst(payload, 67, 10);
    return info;
}

std::vector<bool> EncodeInfo1a(const V34Info1a& info) {
    std::vector<bool> bits;
    bits.reserve(70);
    bits.insert(bits.end(), {true, true, true, true});
    bits.insert(bits.end(), kFrameSync, kFrameSync + 8);

    std::vector<bool> payload;
    payload.reserve(38);
    AppendBitsLsbFirst(payload, info.minPowerReductionDb, 3);
    AppendBitsLsbFirst(payload, info.additionalPowerReductionDb, 3);
    AppendBitsLsbFirst(payload, info.mdLengthIn35msUnits, 7);
    payload.push_back(info.highCarrier);
    AppendBitsLsbFirst(payload, info.preEmphasisFilter, 4);
    AppendBitsLsbFirst(payload, info.projectedDataRateMultiplier, 4);
    AppendBitsLsbFirst(payload, info.symbolRateAnswerToCall, 3);
    AppendBitsLsbFirst(payload, info.symbolRateCallToAnswer, 3);
    AppendSignedLsbFirst(payload, info.frequencyOffsetRaw, 10);

    bits.insert(bits.end(), payload.begin(), payload.end());

    const std::uint16_t crc = ComputeV34Crc16(payload);
    AppendBitsLsbFirst(bits, crc, 16);

    bits.insert(bits.end(), {true, true, true, true});
    return bits;
}

std::optional<V34Info1a> DecodeInfo1a(const std::vector<bool>& bits) {
    constexpr size_t kTotalBits = 70;
    constexpr size_t kPayloadStart = 12;
    constexpr size_t kPayloadBits = 38;
    constexpr size_t kCrcStart = 50;

    if (bits.size() != kTotalBits) {
        return std::nullopt;
    }
    for (size_t i = 0; i < 8; ++i) {
        if (bits[4 + i] != kFrameSync[i]) {
            return std::nullopt;
        }
    }

    const std::vector<bool> payload(bits.begin() + static_cast<long>(kPayloadStart),
                                     bits.begin() + static_cast<long>(kPayloadStart + kPayloadBits));
    const std::uint16_t receivedCrc = static_cast<std::uint16_t>(ReadBitsLsbFirst(bits, kCrcStart, 16));
    if (receivedCrc != ComputeV34Crc16(payload)) {
        return std::nullopt;
    }

    V34Info1a info;
    info.minPowerReductionDb = static_cast<std::uint8_t>(ReadBitsLsbFirst(payload, 0, 3));
    info.additionalPowerReductionDb = static_cast<std::uint8_t>(ReadBitsLsbFirst(payload, 3, 3));
    info.mdLengthIn35msUnits = static_cast<std::uint8_t>(ReadBitsLsbFirst(payload, 6, 7));
    info.highCarrier = payload[13];
    info.preEmphasisFilter = static_cast<std::uint8_t>(ReadBitsLsbFirst(payload, 14, 4));
    info.projectedDataRateMultiplier = static_cast<std::uint8_t>(ReadBitsLsbFirst(payload, 18, 4));
    info.symbolRateAnswerToCall = static_cast<std::uint8_t>(ReadBitsLsbFirst(payload, 22, 3));
    info.symbolRateCallToAnswer = static_cast<std::uint8_t>(ReadBitsLsbFirst(payload, 25, 3));
    info.frequencyOffsetRaw = ReadSignedLsbFirst(payload, 28, 10);
    return info;
}

} // namespace voipmodem::dsp
