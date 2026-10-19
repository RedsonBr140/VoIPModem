#include "../src/Lib/Dsp/V34InfoSequence.h"
#include "TestFramework.h"

using namespace voipmodem::dsp;

TEST(V34InfoSequence_AnswerCarrier_RoundTripsDefaultLocalInfo0) {
    const V34Info0 original = MakeDefaultLocalInfo0();

    std::vector<Sample> samples;
    EncodeInfo0ToSamples(original, kAnswerModemInfoCarrierHz, samples);

    const std::optional<V34Info0> decoded = DecodeInfo0FromSamples(samples, kAnswerModemInfoCarrierHz);

    EXPECT_TRUE(decoded.has_value());
    EXPECT_TRUE(!decoded->symbolRate2743Supported);
    EXPECT_TRUE(!decoded->cmeModem);
    EXPECT_EQ(static_cast<int>(decoded->transmitClockSource), 0);
    EXPECT_TRUE(!decoded->acknowledgePriorInfo0);
}

TEST(V34InfoSequence_CallCarrier_RoundTripsAllFieldsSet) {
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
    info.maxSymbolRateStepDifference = 5;
    info.cmeModem = true;
    info.supports1664PointConstellations = true;
    info.transmitClockSource = 3;
    info.acknowledgePriorInfo0 = true;

    std::vector<Sample> samples;
    EncodeInfo0ToSamples(info, kCallModemInfoCarrierHz, samples);

    const std::optional<V34Info0> decoded = DecodeInfo0FromSamples(samples, kCallModemInfoCarrierHz);

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

TEST(V34InfoSequence_Decode_WrongCarrier_FailsToRoundTrip) {
    // Decoding with the *other* role's carrier should not accidentally
    // produce a valid-looking frame -- if it did, the carrier choice
    // wouldn't actually matter, which would defeat the whole point of
    // the answer/call carrier split in the first place.
    const V34Info0 original = MakeDefaultLocalInfo0();

    std::vector<Sample> samples;
    EncodeInfo0ToSamples(original, kAnswerModemInfoCarrierHz, samples);

    const std::optional<V34Info0> decoded = DecodeInfo0FromSamples(samples, kCallModemInfoCarrierHz);
    EXPECT_TRUE(!decoded.has_value());
}

TEST(StreamingInfo0Receiver_ChunkedAudioWithLeadingSilence_DecodesCorrectly) {
    // Real call audio: the receiver is armed well before the other
    // side actually starts transmitting, and frames don't line up with
    // the sequence's own boundaries.
    const V34Info0 original = MakeDefaultLocalInfo0();
    std::vector<Sample> samples(200, 0.0f); // leading silence
    EncodeInfo0ToSamples(original, kAnswerModemInfoCarrierHz, samples);
    samples.insert(samples.end(), 100, 0.0f); // trailing silence too

    StreamingInfo0Receiver receiver(kAnswerModemInfoCarrierHz);
    std::optional<V34Info0> decoded;
    const size_t kFrameSize = 160;
    for (size_t offset = 0; offset < samples.size() && !decoded.has_value(); offset += kFrameSize) {
        const size_t end = std::min(offset + kFrameSize, samples.size());
        const std::vector<Sample> chunk(samples.begin() + static_cast<long>(offset),
                                         samples.begin() + static_cast<long>(end));
        decoded = receiver.ProcessSamples(chunk);
    }

    EXPECT_TRUE(decoded.has_value());
    if (decoded.has_value()) {
        EXPECT_TRUE(!decoded->cmeModem);
        EXPECT_EQ(static_cast<int>(decoded->transmitClockSource), 0);
    }
}

TEST(StreamingInfo0Receiver_NoSignalYet_ReportsNulloptRepeatedly) {
    StreamingInfo0Receiver receiver(kAnswerModemInfoCarrierHz);
    const std::vector<Sample> silence(1600, 0.0f);
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(!receiver.ProcessSamples(silence).has_value());
    }
}

TEST(V34InfoSequence_Decode_TruncatedAudio_ReturnsNullopt) {
    const V34Info0 original = MakeDefaultLocalInfo0();

    std::vector<Sample> samples;
    EncodeInfo0ToSamples(original, kAnswerModemInfoCarrierHz, samples);
    samples.resize(samples.size() / 2);

    const std::optional<V34Info0> decoded = DecodeInfo0FromSamples(samples, kAnswerModemInfoCarrierHz);
    EXPECT_TRUE(!decoded.has_value());
}
