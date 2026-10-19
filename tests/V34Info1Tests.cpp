#include "../src/Lib/Dsp/V34Info1.h"
#include "TestFramework.h"

using namespace voipmodem::dsp;

TEST(V34Info1c_Encode_ProducesExactly109Bits) {
    const std::vector<bool> bits = EncodeInfo1c(MakeDefaultLocalInfo1c());
    EXPECT_EQ(bits.size(), static_cast<size_t>(109));
}

TEST(V34Info1a_Encode_ProducesExactly70Bits) {
    const std::vector<bool> bits = EncodeInfo1a(MakeDefaultLocalInfo1a());
    EXPECT_EQ(bits.size(), static_cast<size_t>(70));
}

TEST(V34Info1c_DefaultLocal_RoundTripsThroughEncodeDecode) {
    const std::vector<bool> bits = EncodeInfo1c(MakeDefaultLocalInfo1c());
    const std::optional<V34Info1c> decoded = DecodeInfo1c(bits);

    EXPECT_TRUE(decoded.has_value());
    EXPECT_EQ(static_cast<int>(decoded->minPowerReductionDb), 0);
    EXPECT_EQ(static_cast<int>(decoded->additionalPowerReductionDb), 0);
    EXPECT_EQ(static_cast<int>(decoded->mdLengthIn35msUnits), 0);
    EXPECT_TRUE(!decoded->rate2400.highCarrier);
    EXPECT_EQ(static_cast<int>(decoded->rate2400.preEmphasisFilter), 0);
    EXPECT_EQ(static_cast<int>(decoded->rate2400.projectedDataRateMultiplier), 0);
    EXPECT_EQ(static_cast<int>(decoded->frequencyOffsetRaw), -512);
}

TEST(V34Info1c_AllFieldsSet_RoundTripThroughEncodeDecode) {
    V34Info1c info;
    info.minPowerReductionDb = 5;
    info.additionalPowerReductionDb = 3;
    info.mdLengthIn35msUnits = 100;
    info.rate2400 = V34ProbingResult{true, 7, 9};
    info.rate2743 = V34ProbingResult{false, 2, 4};
    info.rate2800 = V34ProbingResult{true, 10, 14};
    info.rate3000 = V34ProbingResult{false, 0, 0};
    info.rate3200 = V34ProbingResult{true, 5, 11};
    info.rate3429 = V34ProbingResult{false, 1, 1};
    info.frequencyOffsetRaw = 123;

    const std::vector<bool> bits = EncodeInfo1c(info);
    const std::optional<V34Info1c> decoded = DecodeInfo1c(bits);

    EXPECT_TRUE(decoded.has_value());
    EXPECT_EQ(static_cast<int>(decoded->minPowerReductionDb), 5);
    EXPECT_EQ(static_cast<int>(decoded->additionalPowerReductionDb), 3);
    EXPECT_EQ(static_cast<int>(decoded->mdLengthIn35msUnits), 100);
    EXPECT_TRUE(decoded->rate2400.highCarrier);
    EXPECT_EQ(static_cast<int>(decoded->rate2400.preEmphasisFilter), 7);
    EXPECT_EQ(static_cast<int>(decoded->rate2400.projectedDataRateMultiplier), 9);
    EXPECT_TRUE(decoded->rate2800.highCarrier);
    EXPECT_EQ(static_cast<int>(decoded->rate2800.preEmphasisFilter), 10);
    EXPECT_EQ(static_cast<int>(decoded->rate3429.projectedDataRateMultiplier), 1);
    EXPECT_EQ(static_cast<int>(decoded->frequencyOffsetRaw), 123);
}

TEST(V34Info1c_NegativeFrequencyOffset_RoundTrips) {
    V34Info1c info;
    info.frequencyOffsetRaw = -300;

    const std::optional<V34Info1c> decoded = DecodeInfo1c(EncodeInfo1c(info));
    EXPECT_TRUE(decoded.has_value());
    EXPECT_EQ(static_cast<int>(decoded->frequencyOffsetRaw), -300);
}

TEST(V34Info1c_Decode_CorruptedCrc_ReturnsNullopt) {
    std::vector<bool> bits = EncodeInfo1c(MakeDefaultLocalInfo1c());
    bits[89] = !bits[89]; // first CRC bit
    EXPECT_TRUE(!DecodeInfo1c(bits).has_value());
}

TEST(V34Info1c_Decode_WrongLength_ReturnsNullopt) {
    std::vector<bool> tooShort(108, false);
    EXPECT_TRUE(!DecodeInfo1c(tooShort).has_value());
}

TEST(V34Info1a_DefaultLocal_RoundTripsThroughEncodeDecode) {
    const std::vector<bool> bits = EncodeInfo1a(MakeDefaultLocalInfo1a());
    const std::optional<V34Info1a> decoded = DecodeInfo1a(bits);

    EXPECT_TRUE(decoded.has_value());
    EXPECT_TRUE(!decoded->highCarrier);
    EXPECT_EQ(static_cast<int>(decoded->symbolRateAnswerToCall), 0);
    EXPECT_EQ(static_cast<int>(decoded->symbolRateCallToAnswer), 0);
    EXPECT_EQ(static_cast<int>(decoded->frequencyOffsetRaw), -512);
}

TEST(V34Info1a_AllFieldsSet_RoundTripThroughEncodeDecode) {
    V34Info1a info;
    info.minPowerReductionDb = 2;
    info.additionalPowerReductionDb = 6;
    info.mdLengthIn35msUnits = 42;
    info.highCarrier = true;
    info.preEmphasisFilter = 9;
    info.projectedDataRateMultiplier = 12;
    info.symbolRateAnswerToCall = 5;
    info.symbolRateCallToAnswer = 3;
    info.frequencyOffsetRaw = -200;

    const std::optional<V34Info1a> decoded = DecodeInfo1a(EncodeInfo1a(info));

    EXPECT_TRUE(decoded.has_value());
    EXPECT_EQ(static_cast<int>(decoded->minPowerReductionDb), 2);
    EXPECT_EQ(static_cast<int>(decoded->additionalPowerReductionDb), 6);
    EXPECT_EQ(static_cast<int>(decoded->mdLengthIn35msUnits), 42);
    EXPECT_TRUE(decoded->highCarrier);
    EXPECT_EQ(static_cast<int>(decoded->preEmphasisFilter), 9);
    EXPECT_EQ(static_cast<int>(decoded->projectedDataRateMultiplier), 12);
    EXPECT_EQ(static_cast<int>(decoded->symbolRateAnswerToCall), 5);
    EXPECT_EQ(static_cast<int>(decoded->symbolRateCallToAnswer), 3);
    EXPECT_EQ(static_cast<int>(decoded->frequencyOffsetRaw), -200);
}

TEST(V34Info1a_Decode_CorruptedCrc_ReturnsNullopt) {
    std::vector<bool> bits = EncodeInfo1a(MakeDefaultLocalInfo1a());
    bits[50] = !bits[50]; // first CRC bit
    EXPECT_TRUE(!DecodeInfo1a(bits).has_value());
}
