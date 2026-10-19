#include "../src/Lib/Dsp/V34Constellation.h"
#include "TestFramework.h"

#include <set>

using namespace voipmodem::dsp;

namespace {
long long MagnitudeSquared(std::pair<int, int> p) {
    return static_cast<long long>(p.first) * p.first + static_cast<long long>(p.second) * p.second;
}
} // namespace

TEST(QuarterConstellation_Has416UniquePoints) {
    std::set<std::pair<int, int>> seen;
    for (const auto& p : kQuarterConstellation) {
        seen.insert(p);
    }
    EXPECT_EQ(seen.size(), static_cast<size_t>(416));
}

TEST(QuarterConstellation_Label0IsSmallestMagnitude) {
    EXPECT_TRUE(kQuarterConstellation[0] == std::make_pair(1, 1));
}

TEST(QuarterConstellation_LabelsAreOrderedByMagnitudeThenImaginaryDescending) {
    // Confirms clause 9.1's own stated ordering rule end to end across
    // the whole table, not just label 0 -- the same self-consistency
    // check used to validate the extraction in the first place.
    for (size_t i = 0; i + 1 < kQuarterConstellation.size(); ++i) {
        const auto a = kQuarterConstellation[i];
        const auto b = kQuarterConstellation[i + 1];
        const long long magA = MagnitudeSquared(a);
        const long long magB = MagnitudeSquared(b);
        EXPECT_TRUE(magA < magB || (magA == magB && a.second > b.second));
    }
}

TEST(RotateClockwise90_RotatesCorrectly) {
    EXPECT_TRUE(RotateClockwise90({1, 0}) == std::make_pair(0, -1));
    EXPECT_TRUE(RotateClockwise90({0, 1}) == std::make_pair(1, 0));
    EXPECT_TRUE(RotateClockwise90({5, 3}) == std::make_pair(3, -5));
}

TEST(RotateClockwise90_FourRotationsReturnToOriginal) {
    const std::pair<int, int> original = {7, 11};
    std::pair<int, int> rotated = original;
    for (int i = 0; i < 4; ++i) {
        rotated = RotateClockwise90(rotated);
    }
    EXPECT_TRUE(rotated == original);
}

TEST(RotateClockwise90_PreservesMagnitude) {
    const std::pair<int, int> original = {9, -13};
    EXPECT_EQ(MagnitudeSquared(RotateClockwise90(original)), MagnitudeSquared(original));
}

TEST(BuildConstellation_ProducesExactlyRequestedPointCount) {
    const auto points = BuildConstellation(64);
    EXPECT_EQ(points.size(), static_cast<size_t>(64));
}

TEST(BuildConstellation_AllPointsAreUnique) {
    const auto points = BuildConstellation(64);
    std::set<std::pair<int, int>> seen(points.begin(), points.end());
    EXPECT_EQ(seen.size(), points.size());
}

TEST(BuildConstellation_FirstQuarterMatchesTheSourceTable) {
    const auto points = BuildConstellation(32);
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(points[static_cast<size_t>(i)] == kQuarterConstellation[static_cast<size_t>(i)]);
    }
}

TEST(BuildConstellation_RotatedQuartersAreClockwiseRotationsOfTheFirst) {
    const auto points = BuildConstellation(32);
    for (int i = 0; i < 8; ++i) {
        const auto expected90 = RotateClockwise90(kQuarterConstellation[static_cast<size_t>(i)]);
        EXPECT_TRUE(points[static_cast<size_t>(8 + i)] == expected90);
    }
}

TEST(BuildConstellation_FullSuperconstellationHas1664Points) {
    const auto points = BuildConstellation(1664);
    std::set<std::pair<int, int>> seen(points.begin(), points.end());
    EXPECT_EQ(points.size(), static_cast<size_t>(1664));
    EXPECT_EQ(seen.size(), static_cast<size_t>(1664));
}

TEST(BuildConstellation_RejectsInvalidPointCount) {
    bool threw = false;
    try {
        BuildConstellation(10); // not a multiple of 4
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}
