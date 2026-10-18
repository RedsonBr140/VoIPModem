#include "../src/Lib/Dsp/V34Info0.h"
#include "TestFramework.h"

using namespace voipmodem::dsp;

TEST(V34Info0_Encode_ProducesExactly49Bits) {
    const std::vector<bool> bits = EncodeInfo0(MakeDefaultLocalInfo0());
    EXPECT_EQ(bits.size(), static_cast<size_t>(49));
}

TEST(V34Info0_Encode_FillAndFrameSyncMatchTable14) {
    const std::vector<bool> bits = EncodeInfo0(MakeDefaultLocalInfo0());
    // 0:3 fill = 1111.
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(bits[i]);
    }
    // 4:11 frame sync = 01110010, left-most bit first in time.
    const bool expectedSync[8] = {false, true, true, true, false, false, true, false};
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(bits[4 + i], expectedSync[i]);
    }
    // 45:48 trailing fill = 1111.
    for (size_t i = 45; i < 49; ++i) {
        EXPECT_TRUE(bits[i]);
    }
}

TEST(V34Info0_DefaultLocalInfo_RoundTripsThroughEncodeDecode) {
    const V34Info0 original = MakeDefaultLocalInfo0();
    const std::vector<bool> bits = EncodeInfo0(original);
    const std::optional<V34Info0> decoded = DecodeInfo0(bits);

    EXPECT_TRUE(decoded.has_value());
    EXPECT_TRUE(!decoded->symbolRate2743Supported);
    EXPECT_TRUE(!decoded->symbolRate2800Supported);
    EXPECT_TRUE(!decoded->symbolRate3429Supported);
    EXPECT_TRUE(!decoded->lowCarrier3000Supported);
    EXPECT_TRUE(!decoded->highCarrier3000Supported);
    EXPECT_TRUE(!decoded->lowCarrier3200Supported);
    EXPECT_TRUE(!decoded->highCarrier3200Supported);
    EXPECT_TRUE(!decoded->symbolRate3429Allowed);
    EXPECT_TRUE(!decoded->canReduceTransmitPower);
    EXPECT_EQ(static_cast<int>(decoded->maxSymbolRateStepDifference), 0);
    EXPECT_TRUE(!decoded->cmeModem);
    EXPECT_TRUE(!decoded->supports1664PointConstellations);
    EXPECT_EQ(static_cast<int>(decoded->transmitClockSource), 0);
    EXPECT_TRUE(!decoded->acknowledgePriorInfo0);
}

TEST(V34Info0_AllFieldsSet_RoundTripThroughEncodeDecode) {
    V34Info0 info;
    info.symbolRate2743Supported = true;
    info.symbolRate2800Supported = true;
    info.symbolRate3429Supported = true;
    info.lowCarrier3000Supported = true;
    info.highCarrier3000Supported = true;
    info.lowCarrier3200Supported = true;
    info.highCarrier3200Supported = true;
    info.symbolRate3429Allowed = true;
    info.canReduceTransmitPower = true;
    info.maxSymbolRateStepDifference = 5; // max value per 21:23's 3-bit range (0-5 valid)
    info.cmeModem = true;
    info.supports1664PointConstellations = true;
    info.transmitClockSource = 3; // max value of the 2-bit range
    info.acknowledgePriorInfo0 = true;

    const std::vector<bool> bits = EncodeInfo0(info);
    const std::optional<V34Info0> decoded = DecodeInfo0(bits);

    EXPECT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->symbolRate2743Supported);
    EXPECT_TRUE(decoded->symbolRate2800Supported);
    EXPECT_TRUE(decoded->symbolRate3429Supported);
    EXPECT_TRUE(decoded->lowCarrier3000Supported);
    EXPECT_TRUE(decoded->highCarrier3000Supported);
    EXPECT_TRUE(decoded->lowCarrier3200Supported);
    EXPECT_TRUE(decoded->highCarrier3200Supported);
    EXPECT_TRUE(decoded->symbolRate3429Allowed);
    EXPECT_TRUE(decoded->canReduceTransmitPower);
    EXPECT_EQ(static_cast<int>(decoded->maxSymbolRateStepDifference), 5);
    EXPECT_TRUE(decoded->cmeModem);
    EXPECT_TRUE(decoded->supports1664PointConstellations);
    EXPECT_EQ(static_cast<int>(decoded->transmitClockSource), 3);
    EXPECT_TRUE(decoded->acknowledgePriorInfo0);
}

TEST(V34Info0_Decode_WrongLength_ReturnsNullopt) {
    std::vector<bool> tooShort(48, false);
    EXPECT_TRUE(!DecodeInfo0(tooShort).has_value());

    std::vector<bool> tooLong(50, false);
    EXPECT_TRUE(!DecodeInfo0(tooLong).has_value());
}

TEST(V34Info0_Decode_CorruptedFrameSync_ReturnsNullopt) {
    std::vector<bool> bits = EncodeInfo0(MakeDefaultLocalInfo0());
    bits[4] = !bits[4]; // corrupt the first frame-sync bit
    EXPECT_TRUE(!DecodeInfo0(bits).has_value());
}

TEST(V34Info0_Decode_CorruptedCrc_ReturnsNullopt) {
    std::vector<bool> bits = EncodeInfo0(MakeDefaultLocalInfo0());
    bits[29] = !bits[29]; // corrupt the first CRC bit, payload untouched
    EXPECT_TRUE(!DecodeInfo0(bits).has_value());
}

TEST(V34Info0_Decode_CorruptedPayloadBit_ReturnsNullopt) {
    // A single flipped payload bit should desync the CRC check too --
    // this is the whole point of including one.
    std::vector<bool> bits = EncodeInfo0(MakeDefaultLocalInfo0());
    bits[12] = !bits[12];
    EXPECT_TRUE(!DecodeInfo0(bits).has_value());
}
