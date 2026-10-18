#include "../src/Lib/Dsp/DbpskDemodulator.h"
#include "../src/Lib/Dsp/DbpskModulator.h"
#include "TestFramework.h"

#include <algorithm>

using namespace voipmodem::dsp;

namespace {
// V.34's call-modem INFO carrier and INFO baud rate (10.1.2.3.1),
// confirmed against the real Recommendation text, not assumed -- see
// docs/ROADMAP.md Phase 4.
constexpr double kCarrierHz = 1200.0;
constexpr double kBaudRate = 600.0;
} // namespace

TEST(Dbpsk_BitLoopback_RoundTripsAKnownPattern) {
    const std::vector<bool> originalBits = {false, true, true, false, true, false, false, true, true, true};

    DbpskModulator modulator(kCarrierHz, kBaudRate, 0.8f, kSampleRateHz);
    std::vector<Sample> samples;
    modulator.EmitReferenceSymbol(samples);
    for (bool bit : originalBits) {
        modulator.ModulateBit(samples, bit);
    }

    DbpskDemodulator demodulator(kCarrierHz, kBaudRate, kSampleRateHz);
    const std::vector<bool> decodedBits = demodulator.ProcessSamples(samples);

    EXPECT_EQ(decodedBits.size(), originalBits.size());
    for (size_t i = 0; i < originalBits.size() && i < decodedBits.size(); ++i) {
        EXPECT_EQ(decodedBits[i], originalBits[i]);
    }
}

TEST(Dbpsk_ReferenceSymbolAlone_ProducesNoBit) {
    DbpskModulator modulator(kCarrierHz, kBaudRate, 0.8f, kSampleRateHz);
    std::vector<Sample> samples;
    modulator.EmitReferenceSymbol(samples);

    DbpskDemodulator demodulator(kCarrierHz, kBaudRate, kSampleRateHz);
    const std::vector<bool> decodedBits = demodulator.ProcessSamples(samples);

    EXPECT_EQ(decodedBits.size(), static_cast<size_t>(0));
}

TEST(Dbpsk_AllZeroBits_NeverRotatePhase_StillDecodeCorrectly) {
    // A run of 0 bits means the phase never rotates at all -- this
    // exercises the "no rotation happened" path specifically, not just
    // whichever path a mixed/alternating pattern happens to hit most.
    const std::vector<bool> originalBits(8, false);

    DbpskModulator modulator(kCarrierHz, kBaudRate, 0.8f, kSampleRateHz);
    std::vector<Sample> samples;
    modulator.EmitReferenceSymbol(samples);
    for (bool bit : originalBits) {
        modulator.ModulateBit(samples, bit);
    }

    DbpskDemodulator demodulator(kCarrierHz, kBaudRate, kSampleRateHz);
    const std::vector<bool> decodedBits = demodulator.ProcessSamples(samples);

    EXPECT_EQ(decodedBits.size(), originalBits.size());
    for (bool bit : decodedBits) {
        EXPECT_TRUE(!bit);
    }
}

// Mirrors Fsk_ChunkedDelivery_MatchesWholeBufferDecoding: real call audio
// arrives in small frames, not one big buffer. ProcessSamples carries
// its symbol-boundary state internally across calls, so feeding it
// chunk-by-chunk must decode exactly the same bits as feeding it
// everything at once.
TEST(Dbpsk_ChunkedDelivery_MatchesWholeBufferDecoding) {
    const std::vector<bool> originalBits = {false, true, true, false, true, false, false, true,
                                              true, true, false, false, true, false, true, true};

    DbpskModulator modulator(kCarrierHz, kBaudRate, 0.8f, kSampleRateHz);
    std::vector<Sample> allSamples;
    modulator.EmitReferenceSymbol(allSamples);
    for (bool bit : originalBits) {
        modulator.ModulateBit(allSamples, bit);
    }

    DbpskDemodulator wholeBufferDemodulator(kCarrierHz, kBaudRate, kSampleRateHz);
    const std::vector<bool> expectedBits = wholeBufferDemodulator.ProcessSamples(allSamples);

    DbpskDemodulator chunkedDemodulator(kCarrierHz, kBaudRate, kSampleRateHz);
    std::vector<bool> chunkedBits;
    const size_t kFrameSize = 160; // matches an 8kHz/20ms RTP frame
    for (size_t offset = 0; offset < allSamples.size(); offset += kFrameSize) {
        const size_t end = std::min(offset + kFrameSize, allSamples.size());
        const std::vector<Sample> chunk(allSamples.begin() + static_cast<long>(offset),
                                         allSamples.begin() + static_cast<long>(end));
        const std::vector<bool> newBits = chunkedDemodulator.ProcessSamples(chunk);
        chunkedBits.insert(chunkedBits.end(), newBits.begin(), newBits.end());
    }

    EXPECT_EQ(chunkedBits.size(), expectedBits.size());
    for (size_t i = 0; i < expectedBits.size() && i < chunkedBits.size(); ++i) {
        EXPECT_EQ(chunkedBits[i], expectedBits[i]);
    }
    EXPECT_EQ(chunkedBits.size(), originalBits.size());
    for (size_t i = 0; i < originalBits.size() && i < chunkedBits.size(); ++i) {
        EXPECT_EQ(chunkedBits[i], originalBits[i]);
    }
}

TEST(Dbpsk_BackToBackSequencesShareOneReferencePoint) {
    // 10.1.2.3.1: "When multiple INFO sequences are transmitted as a
    // group, only the first sequence is preceded by a point at an
    // arbitrary carrier phase." Modulate two "sequences" back-to-back
    // with only one reference symbol up front, and confirm both still
    // decode correctly with no re-sync in between.
    const std::vector<bool> firstSequence = {false, true, true, false};
    const std::vector<bool> secondSequence = {true, true, false, true, false};

    DbpskModulator modulator(kCarrierHz, kBaudRate, 0.8f, kSampleRateHz);
    std::vector<Sample> samples;
    modulator.EmitReferenceSymbol(samples);
    for (bool bit : firstSequence) {
        modulator.ModulateBit(samples, bit);
    }
    for (bool bit : secondSequence) {
        modulator.ModulateBit(samples, bit);
    }

    DbpskDemodulator demodulator(kCarrierHz, kBaudRate, kSampleRateHz);
    const std::vector<bool> decodedBits = demodulator.ProcessSamples(samples);

    std::vector<bool> expected = firstSequence;
    expected.insert(expected.end(), secondSequence.begin(), secondSequence.end());

    EXPECT_EQ(decodedBits.size(), expected.size());
    for (size_t i = 0; i < expected.size() && i < decodedBits.size(); ++i) {
        EXPECT_EQ(decodedBits[i], expected[i]);
    }
}
