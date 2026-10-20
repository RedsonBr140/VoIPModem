#include "../src/Lib/Dsp/V34Scrambler.h"
#include "TestFramework.h"

#include <vector>

using namespace voipmodem::dsp;

namespace {

// Hand-derived from the recurrence q[k] = 1 XOR q[k-n1] XOR q[k-n2]
// (q[k-n] treated as 0 when k < n), i.e. feeding all-ones into a
// zero-initialized scrambler -- exactly the TRN-signal condition
// described in clause 10.1.3.8. Independently worked out bit by bit
// (not derived from this implementation) as a correctness oracle for
// the call-mode taps (18, 23):
//   k  0.. 17: 1 (eighteen 1s, neither tap reached yet)
//   k 18.. 22: 0 (near tap reached: 1 XOR q[k-18], and q[k-18] is one
//               of the leading 1s)
//   k 23.. 35: 1 (far tap also reached; both XOR terms are 1, cancel)
//   k 36.. 40: 0
const std::vector<bool> kCallModeOracle = {
    // 0-17
    true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
    // 18-22
    false, false, false, false, false,
    // 23-35
    true, true, true, true, true, true, true, true, true, true, true, true, true,
    // 36-40
    false, false, false, false, false,
};

// Same derivation for the answer-mode taps (5, 23):
const std::vector<bool> kAnswerModeOracle = {
    // 0-4
    true, true, true, true, true,
    // 5-9
    false, false, false, false, false,
    // 10-14
    true, true, true, true, true,
    // 15-19
    false, false, false, false, false,
    // 20-22
    true, true, true,
    // 23-24
    false, false,
    // 25-29
    true, true, true, true, true,
    // 30
    false,
};

} // namespace

TEST(V34Scrambler_CallMode_MatchesHandDerivedSequence) {
    V34Scrambler scrambler(V34ScramblerMode::CallMode);
    for (bool expected : kCallModeOracle) {
        EXPECT_EQ(scrambler.ScrambleBit(true), expected);
    }
}

TEST(V34Scrambler_AnswerMode_MatchesHandDerivedSequence) {
    V34Scrambler scrambler(V34ScramblerMode::AnswerMode);
    for (bool expected : kAnswerModeOracle) {
        EXPECT_EQ(scrambler.ScrambleBit(true), expected);
    }
}

TEST(V34Scrambler_Reset_RestartsSequenceFromZero) {
    V34Scrambler scrambler(V34ScramblerMode::CallMode);
    for (int i = 0; i < 30; ++i) {
        scrambler.ScrambleBit(true);
    }
    scrambler.Reset();
    for (bool expected : kCallModeOracle) {
        EXPECT_EQ(scrambler.ScrambleBit(true), expected);
    }
}

namespace {
std::vector<bool> MakePseudoRandomData(size_t count) {
    std::vector<bool> data;
    data.reserve(count);
    uint32_t state = 0x12345u;
    for (size_t i = 0; i < count; ++i) {
        // Simple xorshift -- just needs to not be a trivial fixed
        // pattern so the round-trip test isn't vacuous.
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        data.push_back((state & 1u) != 0);
    }
    return data;
}
} // namespace

TEST(V34ScramblerDescrambler_CallMode_RoundTrip) {
    const std::vector<bool> data = MakePseudoRandomData(500);
    V34Scrambler scrambler(V34ScramblerMode::CallMode);
    V34Descrambler descrambler(V34ScramblerMode::CallMode);

    std::vector<bool> recovered;
    recovered.reserve(data.size());
    for (bool bit : data) {
        const bool scrambled = scrambler.ScrambleBit(bit);
        recovered.push_back(descrambler.DescrambleBit(scrambled));
    }

    EXPECT_EQ(recovered.size(), data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        EXPECT_EQ(recovered[i], data[i]);
    }
}

TEST(V34ScramblerDescrambler_AnswerMode_RoundTrip) {
    const std::vector<bool> data = MakePseudoRandomData(500);
    V34Scrambler scrambler(V34ScramblerMode::AnswerMode);
    V34Descrambler descrambler(V34ScramblerMode::AnswerMode);

    std::vector<bool> recovered;
    recovered.reserve(data.size());
    for (bool bit : data) {
        const bool scrambled = scrambler.ScrambleBit(bit);
        recovered.push_back(descrambler.DescrambleBit(scrambled));
    }

    for (size_t i = 0; i < data.size(); ++i) {
        EXPECT_EQ(recovered[i], data[i]);
    }
}

// Exercises the actual self-synchronizing property: a descrambler that
// starts at a zero history NOT matching the transmitter's true state at
// that point in the stream (because it joins mid-stream, as after a
// dropout) still recovers correctly once it has consumed
// max(n1, n2) = 23 genuine scrambled bits, with no shared initial state
// required.
TEST(V34Descrambler_SelfSynchronizesAfterJoiningMidStream) {
    const std::vector<bool> data = MakePseudoRandomData(100);
    V34Scrambler scrambler(V34ScramblerMode::CallMode);
    std::vector<bool> scrambledStream;
    scrambledStream.reserve(data.size());
    for (bool bit : data) {
        scrambledStream.push_back(scrambler.ScrambleBit(bit));
    }

    const size_t dropStart = 30; // well past the initial transient
    V34Descrambler lateJoiner(V34ScramblerMode::CallMode); // starts at zero history, NOT the true state at dropStart

    std::vector<bool> recovered;
    for (size_t i = dropStart; i < scrambledStream.size(); ++i) {
        recovered.push_back(lateJoiner.DescrambleBit(scrambledStream[i]));
    }

    // After the first 23 bits (the transient while history fills with
    // genuine fed values), every subsequent bit must be recovered
    // correctly.
    for (size_t i = 23; i < recovered.size(); ++i) {
        EXPECT_EQ(recovered[i], data[dropStart + i]);
    }
}
