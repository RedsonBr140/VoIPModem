#include "../src/Lib/Dsp/FskDemodulator.h"
#include "../src/Lib/Dsp/FskModulator.h"
#include "TestFramework.h"

#include <algorithm>

using namespace voipmodem::dsp;

namespace {
// V.21 channel 1: the frequency pair the calling party transmits CI/CM
// on (confirmed against V.21's own frequency table, not assumed -- see
// docs/ROADMAP.md Phase 4).
constexpr double kMarkHz = 980.0;
constexpr double kSpaceHz = 1180.0;
constexpr double kBaudRate = 300.0;
} // namespace

TEST(Fsk_BitLoopback_RoundTripsAKnownPattern) {
    const std::vector<bool> originalBits = {false, true, true, false, true, false, false, true, true, true};

    FskModulator modulator(kMarkHz, kSpaceHz, kBaudRate, 0.8f, kSampleRateHz);
    std::vector<Sample> samples;
    for (bool bit : originalBits) {
        modulator.ModulateBit(samples, bit);
    }

    FskDemodulator demodulator(kMarkHz, kSpaceHz, kBaudRate, kSampleRateHz);
    const std::vector<bool> decodedBits = demodulator.ProcessSamples(samples);

    EXPECT_EQ(decodedBits.size(), originalBits.size());
    for (size_t i = 0; i < originalBits.size(); ++i) {
        EXPECT_EQ(decodedBits[i], originalBits[i]);
    }
}

TEST(Fsk_AsyncByteLoopback_RoundTripsSeveralBytes) {
    const std::vector<std::uint8_t> originalBytes = {0x00, 0xFF, 0xA5, 0x81, 0x55};

    FskModulator modulator(kMarkHz, kSpaceHz, kBaudRate, 0.8f, kSampleRateHz);
    std::vector<Sample> samples;
    for (std::uint8_t byte : originalBytes) {
        modulator.ModulateByteAsync(samples, byte);
    }

    FskDemodulator demodulator(kMarkHz, kSpaceHz, kBaudRate, kSampleRateHz);
    const std::vector<bool> bits = demodulator.ProcessSamples(samples);

    size_t cursor = 0;
    for (std::uint8_t expectedByte : originalBytes) {
        const std::optional<std::uint8_t> decoded = FskDemodulator::TryDecodeAsyncByte(bits, cursor);
        EXPECT_TRUE(decoded.has_value());
        EXPECT_EQ(static_cast<int>(decoded.value_or(0xFF)), static_cast<int>(expectedByte));
    }
}

TEST(Fsk_TryDecodeAsyncByte_MisalignedStart_ReturnsNulloptWithoutAdvancing) {
    // A start bit must be space/0; a run of all-mark bits never contains
    // one, so this should fail cleanly rather than guess at a byte.
    const std::vector<bool> allMarkBits(10, true);
    size_t cursor = 0;
    const auto decoded = FskDemodulator::TryDecodeAsyncByte(allMarkBits, cursor);

    EXPECT_TRUE(!decoded.has_value());
    EXPECT_EQ(cursor, static_cast<size_t>(0));
}

TEST(Fsk_TryDecodeAsyncByte_NotEnoughBits_ReturnsNullopt) {
    const std::vector<bool> tooFewBits(5, false);
    size_t cursor = 0;
    const auto decoded = FskDemodulator::TryDecodeAsyncByte(tooFewBits, cursor);

    EXPECT_TRUE(!decoded.has_value());
    EXPECT_EQ(cursor, static_cast<size_t>(0));
}

// Exercises the exact pattern CallAudioBridge needs: real call audio
// arrives in small (20ms/160-sample) frames, not one big buffer, for as
// long as the call stays up. ProcessSamples carries its bit-boundary
// state internally across calls, so feeding it chunk-by-chunk must
// decode exactly the same bits as feeding it everything at once -- with
// no growing buffer and no re-processing of samples already consumed.
// (An earlier version of FskDemodulator required the *caller* to keep a
// growing buffer and re-decode all of it every call; that version passed
// this same test in isolation but, once wired into a real call, turned
// into unbounded per-frame CPU growth that hung a live call after about
// a minute and a half -- see docs/ROADMAP.md Phase 4. This test's job
// now is to keep that from regressing.)
TEST(Fsk_ChunkedDelivery_MatchesWholeBufferDecoding) {
    const std::vector<std::uint8_t> originalBytes = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    FskModulator modulator(kMarkHz, kSpaceHz, kBaudRate, 0.8f, kSampleRateHz);
    std::vector<Sample> allSamples;
    for (std::uint8_t byte : originalBytes) {
        modulator.ModulateByteAsync(allSamples, byte);
    }

    FskDemodulator wholeBufferDemodulator(kMarkHz, kSpaceHz, kBaudRate, kSampleRateHz);
    const std::vector<bool> expectedBits = wholeBufferDemodulator.ProcessSamples(allSamples);

    FskDemodulator chunkedDemodulator(kMarkHz, kSpaceHz, kBaudRate, kSampleRateHz);
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

    size_t cursor = 0;
    std::vector<std::uint8_t> decodedBytes;
    while (true) {
        const auto decoded = FskDemodulator::TryDecodeAsyncByte(chunkedBits, cursor);
        if (!decoded.has_value()) {
            break;
        }
        decodedBytes.push_back(decoded.value());
    }

    EXPECT_EQ(decodedBytes.size(), originalBytes.size());
    for (size_t i = 0; i < originalBytes.size() && i < decodedBytes.size(); ++i) {
        EXPECT_EQ(static_cast<int>(decodedBytes[i]), static_cast<int>(originalBytes[i]));
    }
}

// Symbol-timing recovery (FskDemodulator.h's class comment) exists to
// track a slow phase drift between transmitter and receiver, not a
// single fixed start-of-stream offset -- tried that first and found
// this demodulator's threshold-comparison decision is already fairly
// forgiving of a static sub-bit misalignment on its own. The honest
// source of real drift here isn't a mismatched crystal (there isn't
// one -- both ends are computed at the same nominal 8kHz, see
// DspTypes.h) but a VoIP jitter buffer's own accelerate/expand
// concealment, which genuinely plays audio back very slightly faster
// or slower than it was recorded to adapt its buffer level. This test
// stands in for that: it modulates at a baud rate very slightly off
// the demodulator's configured nominal rate, so the true bit edges
// drift steadily away from the receiver's assumed grid over a long
// stream -- enough drift, left uncorrected, to eventually land on the
// wrong side of a transition and flip a decision.
TEST(Fsk_SymbolTimingRecovery_TracksSlowBaudMismatch) {
    std::vector<std::uint8_t> originalBytes;
    for (int i = 0; i < 40; ++i) {
        originalBytes.push_back(static_cast<std::uint8_t>((i * 97 + 13) & 0xFF)); // varied, not a fixed pattern
    }

    // 0.6% fast: small enough to be a plausible jitter-buffer
    // concealment rate, large enough to accumulate several samples of
    // drift over this many bits.
    FskModulator modulator(kMarkHz, kSpaceHz, kBaudRate * 1.006, 0.8f, kSampleRateHz);
    std::vector<Sample> allSamples;
    for (std::uint8_t byte : originalBytes) {
        modulator.ModulateByteAsync(allSamples, byte);
    }

    FskDemodulator demodulator(kMarkHz, kSpaceHz, kBaudRate, kSampleRateHz); // unaware of the mismatch
    const std::vector<bool> bits = demodulator.ProcessSamples(allSamples);

    size_t cursor = 0;
    std::vector<std::uint8_t> decodedBytes;
    while (true) {
        const auto decoded = FskDemodulator::TryDecodeAsyncByte(bits, cursor);
        if (!decoded.has_value()) {
            break;
        }
        decodedBytes.push_back(decoded.value());
    }

    // Drift accumulates forward in time, so any truncation from
    // rounding belongs at the very end, not the start -- compare from
    // the front, tolerating a short tail this test doesn't require.
    constexpr size_t kTailToleranceBytes = 2;
    EXPECT_TRUE(decodedBytes.size() + kTailToleranceBytes >= originalBytes.size());

    const size_t compareCount = std::min(decodedBytes.size(), originalBytes.size());
    for (size_t i = 0; i < compareCount; ++i) {
        EXPECT_EQ(static_cast<int>(decodedBytes[i]), static_cast<int>(originalBytes[i]));
    }
}
