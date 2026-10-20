#include "../src/Lib/Dsp/V34Trn.h"
#include "../src/Lib/Dsp/V34Constellation.h"
#include "TestFramework.h"

#include <utility>
#include <vector>

using namespace voipmodem::dsp;

namespace {
std::pair<int, int> RotateClockwiseSteps(std::pair<int, int> point, int steps) {
    for (int i = 0; i < steps; ++i) {
        point = RotateClockwise90(point);
    }
    return point;
}
} // namespace

// Both oracles below are derived from the SAME hand-derived call-mode
// scrambler sequence used in V34ScramblerTests.cpp (q[0..40], all-ones
// input, zero-initialized -- the TRN condition per clause 10.1.3.8),
// just regrouped into symbols per the 4-point/16-point extraction
// rules in that clause, independently of V34TrnGenerator's own code.

TEST(V34TrnGenerator_4Point_CallMode_MatchesHandDerivedSequence) {
    // Symbols consume (I1n, I2n) pairs from q[0..39]; In = 2*I2n + I1n.
    // q itself: 1x18, 0x5, 1x13, 0x5 (see V34ScramblerTests.cpp).
    const std::vector<int> expectedIn = {3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 2, 3, 3, 3, 3, 3, 3, 0, 0};

    V34TrnGenerator trn(V34ScramblerMode::CallMode, /*use16Point=*/false);
    for (int in : expectedIn) {
        const auto expectedPoint = RotateClockwiseSteps(kQuarterConstellation[0], in);
        EXPECT_TRUE(trn.NextPoint() == expectedPoint);
    }
}

TEST(V34TrnGenerator_16Point_CallMode_MatchesHandDerivedSequence) {
    // Symbols consume (I1n, I2n, Q1n, Q2n) quadruples from the same
    // q[0..39]; In = 2*I2n + I1n selects the rotation, baseIndex =
    // 2*Q2n + Q1n selects the point out of kQuarterConstellation[0..3].
    const std::vector<std::pair<int, int>> expectedBaseAndIn = {
        {3, 3}, {3, 3}, {3, 3}, {3, 3}, {0, 3}, {2, 0}, {3, 3}, {3, 3}, {3, 3}, {0, 0},
    };

    V34TrnGenerator trn(V34ScramblerMode::CallMode, /*use16Point=*/true);
    for (const auto& [baseIndex, in] : expectedBaseAndIn) {
        const auto expectedPoint = RotateClockwiseSteps(kQuarterConstellation[static_cast<size_t>(baseIndex)], in);
        EXPECT_TRUE(trn.NextPoint() == expectedPoint);
    }
}

TEST(V34TrnGenerator_Reset_RestartsSequenceFromZero) {
    V34TrnGenerator trn(V34ScramblerMode::AnswerMode, /*use16Point=*/true);
    const auto first = trn.NextPoint();
    for (int i = 0; i < 20; ++i) {
        trn.NextPoint();
    }
    trn.Reset();
    EXPECT_TRUE(trn.NextPoint() == first);
}

TEST(V34TrnGenerator_4PointOutputsAreAlwaysRotationsOfPointZero) {
    V34TrnGenerator trn(V34ScramblerMode::CallMode, /*use16Point=*/false);
    for (int i = 0; i < 64; ++i) {
        const auto point = trn.NextPoint();
        bool matchesSomeRotation = false;
        for (int steps = 0; steps < 4; ++steps) {
            if (point == RotateClockwiseSteps(kQuarterConstellation[0], steps)) {
                matchesSomeRotation = true;
            }
        }
        EXPECT_TRUE(matchesSomeRotation);
    }
}

TEST(V34TrnGenerator_16PointOutputsAreAlwaysRotationsOfFirstFourPoints) {
    V34TrnGenerator trn(V34ScramblerMode::CallMode, /*use16Point=*/true);
    for (int i = 0; i < 64; ++i) {
        const auto point = trn.NextPoint();
        bool matchesSomeRotation = false;
        for (int base = 0; base < 4; ++base) {
            for (int steps = 0; steps < 4; ++steps) {
                if (point == RotateClockwiseSteps(kQuarterConstellation[static_cast<size_t>(base)], steps)) {
                    matchesSomeRotation = true;
                }
            }
        }
        EXPECT_TRUE(matchesSomeRotation);
    }
}
