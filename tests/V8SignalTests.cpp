#include "../src/Lib/Dsp/V8Signal.h"
#include "TestFramework.h"

using namespace voipmodem::dsp;

namespace {
constexpr double kChannel1MarkHz = 980.0;
constexpr double kChannel1SpaceHz = 1180.0;
constexpr double kBaudRate = 300.0;

std::vector<bool> CmJmPreamble() {
    std::vector<bool> pattern(kV8PreambleOnes, kV8PreambleOnes + 10);
    pattern.insert(pattern.end(), kV8CmJmSyncBits, kV8CmJmSyncBits + 10);
    return pattern;
}
} // namespace

TEST(V8Signal_FindBitPattern_LocatesExactMatch) {
    std::vector<bool> haystack = {false, true, false, true, true, true, false, false};
    const std::vector<bool> pattern = {true, true, true};
    const auto found = FindBitPattern(haystack, pattern, 0);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found.value(), static_cast<size_t>(3));
}

TEST(V8Signal_FindBitPattern_NoMatchReturnsNullopt) {
    std::vector<bool> haystack = {false, false, false};
    const std::vector<bool> pattern = {true, true};
    EXPECT_TRUE(!FindBitPattern(haystack, pattern, 0).has_value());
}

TEST(V8Signal_CmSequence_PreambleThenMenuRoundTripsThroughBits) {
    V8CallMenu menu;
    menu.callFunction = V8CallFunction::Data;
    V8ModulationModes modes;
    modes.v34Duplex = true;
    modes.v21 = true;
    menu.modulationModes = modes;

    FskModulator txModulator(kChannel1MarkHz, kChannel1SpaceHz, kBaudRate, 0.8f, kSampleRateHz);
    // CM/JM are continuously repeated on the real wire (3.4/3.6); a
    // single, non-repeating sequence can never be distinguished from
    // "still arriving" -- there's nothing after it to signal a genuine
    // framing failure (ReadCallMenuAfterPreamble's real end-of-sequence
    // marker). Sending two back-to-back, like CM actually behaves,
    // gives the first one something real to terminate against.
    std::vector<Sample> samples = BuildCmOrJmSequence(txModulator, menu);
    const std::vector<Sample> second = BuildCmOrJmSequence(txModulator, menu);
    samples.insert(samples.end(), second.begin(), second.end());

    FskDemodulator rxDemodulator(kChannel1MarkHz, kChannel1SpaceHz, kBaudRate, kSampleRateHz);
    const std::vector<bool> bits = rxDemodulator.ProcessSamples(samples);

    const std::vector<bool> preamble = CmJmPreamble();
    const auto found = FindBitPattern(bits, preamble, 0);
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found.value(), static_cast<size_t>(0));

    size_t index = found.value() + preamble.size();
    const auto decodedMenu = ReadCallMenuAfterPreamble(bits, index);
    EXPECT_TRUE(decodedMenu.has_value());
    EXPECT_EQ(static_cast<int>(decodedMenu.value().callFunction), static_cast<int>(V8CallFunction::Data));
    EXPECT_TRUE(decodedMenu.value().modulationModes.has_value());
    EXPECT_TRUE(decodedMenu.value().modulationModes.value().v34Duplex);
    EXPECT_TRUE(decodedMenu.value().modulationModes.value().v21);
    // index now sits exactly where the second sequence's preamble
    // starts -- the genuine framing failure that terminated the first.
    EXPECT_EQ(index, found.value() + preamble.size() + static_cast<size_t>(40)); // 4 octets * 10 bits
    EXPECT_TRUE(index < bits.size());
}

TEST(V8Signal_TwoConsecutiveCmSequences_SecondPreambleFoundRightAfterFirst) {
    V8CallMenu menu;
    menu.callFunction = V8CallFunction::Data;

    FskModulator txModulator(kChannel1MarkHz, kChannel1SpaceHz, kBaudRate, 0.8f, kSampleRateHz);
    std::vector<Sample> samples = BuildCmOrJmSequence(txModulator, menu);
    const std::vector<Sample> second = BuildCmOrJmSequence(txModulator, menu);
    samples.insert(samples.end(), second.begin(), second.end());

    FskDemodulator rxDemodulator(kChannel1MarkHz, kChannel1SpaceHz, kBaudRate, kSampleRateHz);
    const std::vector<bool> bits = rxDemodulator.ProcessSamples(samples);
    const std::vector<bool> preamble = CmJmPreamble();

    const auto firstFound = FindBitPattern(bits, preamble, 0);
    EXPECT_TRUE(firstFound.has_value());
    size_t index = firstFound.value() + preamble.size();
    const auto firstMenu = ReadCallMenuAfterPreamble(bits, index);
    EXPECT_TRUE(firstMenu.has_value());

    const auto secondFound = FindBitPattern(bits, preamble, index);
    EXPECT_TRUE(secondFound.has_value());
    EXPECT_EQ(secondFound.value(), index); // the second sequence starts exactly where the first one's octets ended
}

TEST(V8Signal_CjSequence_LooksLikeCjAtStart) {
    FskModulator txModulator(kChannel1MarkHz, kChannel1SpaceHz, kBaudRate, 0.8f, kSampleRateHz);
    const std::vector<Sample> samples = BuildCjSequence(txModulator);

    FskDemodulator rxDemodulator(kChannel1MarkHz, kChannel1SpaceHz, kBaudRate, kSampleRateHz);
    const std::vector<bool> bits = rxDemodulator.ProcessSamples(samples);

    EXPECT_TRUE(LooksLikeCj(bits, 0));
}

TEST(V8Signal_NonCjBits_DoesNotLookLikeCj) {
    V8CallMenu menu;
    menu.callFunction = V8CallFunction::Data; // non-zero octet -> not CJ

    FskModulator txModulator(kChannel1MarkHz, kChannel1SpaceHz, kBaudRate, 0.8f, kSampleRateHz);
    std::vector<Sample> samples;
    txModulator.ModulateByteAsync(samples, EncodeCallFunctionOctet(menu.callFunction));
    txModulator.ModulateByteAsync(samples, 0x00);
    txModulator.ModulateByteAsync(samples, 0x00);

    FskDemodulator rxDemodulator(kChannel1MarkHz, kChannel1SpaceHz, kBaudRate, kSampleRateHz);
    const std::vector<bool> bits = rxDemodulator.ProcessSamples(samples);

    EXPECT_TRUE(!LooksLikeCj(bits, 0));
}
