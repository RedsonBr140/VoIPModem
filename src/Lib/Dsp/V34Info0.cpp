#include "V34Info0.h"

#include <cstdint>

#include "V34Crc.h"

namespace voipmodem::dsp {

namespace {

constexpr size_t kTotalBits = 49;
constexpr size_t kFrameSyncStart = 4;
constexpr bool kFrameSync[8] = {false, true, true, true, false, false, true, false}; // 01110010, left-most bit first
constexpr size_t kPayloadStart = 12;
constexpr size_t kPayloadBits = 17; // bits 12:28 inclusive
constexpr size_t kCrcStart = 29; // bits 29:44, 16 bits

void AppendLsbFirst(std::vector<bool>& bits, std::uint8_t value, int bitCount) {
    for (int i = 0; i < bitCount; ++i) {
        bits.push_back(((value >> i) & 0x01) != 0);
    }
}

std::uint8_t ReadLsbFirst(const std::vector<bool>& bits, size_t startIndex, int bitCount) {
    std::uint8_t value = 0;
    for (int i = 0; i < bitCount; ++i) {
        if (bits[startIndex + static_cast<size_t>(i)]) {
            value |= static_cast<std::uint8_t>(1u << i);
        }
    }
    return value;
}

} // namespace

V34Info0 MakeDefaultLocalInfo0() {
    return V34Info0{};
}

std::vector<bool> EncodeInfo0(const V34Info0& info) {
    std::vector<bool> bits;
    bits.reserve(kTotalBits);

    bits.insert(bits.end(), {true, true, true, true}); // fill(0:3)
    bits.insert(bits.end(), kFrameSync, kFrameSync + 8); // frame sync(4:11)

    std::vector<bool> payload;
    payload.reserve(kPayloadBits);
    payload.push_back(info.symbolRate2743Supported);     // 12
    payload.push_back(info.symbolRate2800Supported);     // 13
    payload.push_back(info.symbolRate3429Supported);     // 14
    payload.push_back(info.lowCarrier3000Supported);     // 15
    payload.push_back(info.highCarrier3000Supported);    // 16
    payload.push_back(info.lowCarrier3200Supported);     // 17
    payload.push_back(info.highCarrier3200Supported);    // 18
    payload.push_back(info.symbolRate3429Allowed);       // 19
    payload.push_back(info.canReduceTransmitPower);      // 20
    AppendLsbFirst(payload, info.maxSymbolRateStepDifference, 3); // 21:23
    payload.push_back(info.cmeModem);                     // 24
    payload.push_back(info.supports1664PointConstellations); // 25
    AppendLsbFirst(payload, info.transmitClockSource, 2);  // 26:27
    payload.push_back(info.acknowledgePriorInfo0);         // 28

    bits.insert(bits.end(), payload.begin(), payload.end());

    const std::uint16_t crc = ComputeV34Crc16(payload);
    AppendLsbFirst(bits, static_cast<std::uint8_t>(crc & 0xFF), 8);
    AppendLsbFirst(bits, static_cast<std::uint8_t>((crc >> 8) & 0xFF), 8);

    bits.insert(bits.end(), {true, true, true, true}); // fill(45:48)

    return bits;
}

std::optional<V34Info0> DecodeInfo0(const std::vector<bool>& bits) {
    if (bits.size() != kTotalBits) {
        return std::nullopt;
    }

    for (size_t i = 0; i < 8; ++i) {
        if (bits[kFrameSyncStart + i] != kFrameSync[i]) {
            return std::nullopt;
        }
    }

    const std::vector<bool> payload(bits.begin() + static_cast<long>(kPayloadStart),
                                     bits.begin() + static_cast<long>(kPayloadStart + kPayloadBits));
    const std::uint16_t receivedCrc = static_cast<std::uint16_t>(ReadLsbFirst(bits, kCrcStart, 8)) |
                                       static_cast<std::uint16_t>(ReadLsbFirst(bits, kCrcStart + 8, 8) << 8);
    if (receivedCrc != ComputeV34Crc16(payload)) {
        return std::nullopt;
    }

    V34Info0 info;
    info.symbolRate2743Supported = payload[0];
    info.symbolRate2800Supported = payload[1];
    info.symbolRate3429Supported = payload[2];
    info.lowCarrier3000Supported = payload[3];
    info.highCarrier3000Supported = payload[4];
    info.lowCarrier3200Supported = payload[5];
    info.highCarrier3200Supported = payload[6];
    info.symbolRate3429Allowed = payload[7];
    info.canReduceTransmitPower = payload[8];
    info.maxSymbolRateStepDifference = ReadLsbFirst(payload, 9, 3);
    info.cmeModem = payload[12];
    info.supports1664PointConstellations = payload[13];
    info.transmitClockSource = ReadLsbFirst(payload, 14, 2);
    info.acknowledgePriorInfo0 = payload[16];

    return info;
}

} // namespace voipmodem::dsp
