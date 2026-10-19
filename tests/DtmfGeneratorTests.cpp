#include "../src/Lib/Dsp/DtmfGenerator.h"
#include "../src/Lib/Dsp/GoertzelDetector.h"
#include "TestFramework.h"

using namespace voipmodem::dsp;

namespace {
constexpr Sample kAmplitude = 0.8f;

double GoertzelMagnitudeAt(const std::vector<Sample>& samples, double hz) {
    GoertzelDetector detector(hz, kSampleRateHz);
    for (Sample s : samples) {
        detector.Process(s);
    }
    return detector.Magnitude(static_cast<int>(samples.size()));
}
} // namespace

TEST(GetDtmfFrequencies_KnownDigitsMapToTheStandardKeypad) {
    EXPECT_TRUE(GetDtmfFrequencies('1') == std::make_pair(697.0, 1209.0));
    EXPECT_TRUE(GetDtmfFrequencies('5') == std::make_pair(770.0, 1336.0));
    EXPECT_TRUE(GetDtmfFrequencies('9') == std::make_pair(852.0, 1477.0));
    EXPECT_TRUE(GetDtmfFrequencies('0') == std::make_pair(941.0, 1336.0));
    EXPECT_TRUE(GetDtmfFrequencies('*') == std::make_pair(941.0, 1209.0));
    EXPECT_TRUE(GetDtmfFrequencies('#') == std::make_pair(941.0, 1477.0));
}

TEST(GetDtmfFrequencies_InvalidCharacter_ReturnsNullopt) {
    EXPECT_TRUE(!GetDtmfFrequencies('X').has_value());
    EXPECT_TRUE(!GetDtmfFrequencies(' ').has_value());
}

TEST(GenerateDtmfDigit_ProducesEnergyAtBothExpectedFrequencies) {
    std::vector<Sample> samples;
    GenerateDtmfDigit('5', 0.08, kAmplitude, samples);

    EXPECT_TRUE(!samples.empty());
    const double rowMag = GoertzelMagnitudeAt(samples, 770.0);
    const double colMag = GoertzelMagnitudeAt(samples, 1336.0);
    EXPECT_TRUE(rowMag > 0.2);
    EXPECT_TRUE(colMag > 0.2);

    // A different digit's own frequencies shouldn't show up.
    const double wrongMag = GoertzelMagnitudeAt(samples, 1633.0);
    EXPECT_TRUE(wrongMag < 0.05);
}

TEST(GenerateDtmfDigit_InvalidDigit_AppendsNothing) {
    std::vector<Sample> samples;
    GenerateDtmfDigit('X', 0.08, kAmplitude, samples);
    EXPECT_EQ(samples.size(), static_cast<size_t>(0));
}

TEST(GenerateDtmfDigit_DurationMatchesSampleCount) {
    std::vector<Sample> samples;
    GenerateDtmfDigit('1', 0.1, kAmplitude, samples);
    EXPECT_EQ(samples.size(), static_cast<size_t>(800)); // 0.1s @ 8kHz
}

TEST(GenerateDialTone_ProducesEnergyAt350And440Hz) {
    std::vector<Sample> samples;
    GenerateDialTone(0.2, kAmplitude, samples);

    EXPECT_TRUE(!samples.empty());
    EXPECT_TRUE(GoertzelMagnitudeAt(samples, 350.0) > 0.2);
    EXPECT_TRUE(GoertzelMagnitudeAt(samples, 440.0) > 0.2);
    EXPECT_TRUE(GoertzelMagnitudeAt(samples, 1000.0) < 0.05);
}
