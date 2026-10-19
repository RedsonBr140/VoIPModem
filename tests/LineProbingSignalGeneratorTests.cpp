#include "../src/Lib/Dsp/LineProbingSignalGenerator.h"
#include "../src/Lib/Dsp/Dds.h"
#include "../src/Lib/Dsp/GoertzelDetector.h"
#include "TestFramework.h"

#include <cmath>

using namespace voipmodem::dsp;

namespace {
constexpr Sample kNominalAmplitude = 0.8f;
// 160ms at 8kHz = 1280 samples = exactly 24 repetitions of the 150Hz
// fundamental, and therefore an exact integer number of cycles of
// EVERY tone in Table 17 (each is k*150Hz for some integer k) --
// chosen deliberately so a Goertzel analysis over this exact block
// length is leakage-free, not an approximation.
constexpr int kL1DurationSamples = 1280;
constexpr double kExpectedPerToneMagnitude = 0.8 / 4.58257569495584; // kNominalAmplitude / sqrt(21)

double GoertzelMagnitudeAt(const std::vector<Sample>& samples, double hz) {
    GoertzelDetector detector(hz, kSampleRateHz);
    for (Sample s : samples) {
        detector.Process(s);
    }
    return detector.Magnitude(static_cast<int>(samples.size()));
}

// Correlates `samples` (exactly `samples.size()` long) against a local
// cosine/sine reference at `hz`, returning normalized (I, Q) -- I is
// the cosine-aligned component Table 17's cos(2*pi*f*t + theta)
// definition cares about, Q should read ~0 for a pure, correctly
// cosine-phased tone over an integer number of cycles.
void CorrelateCosine(const std::vector<Sample>& samples, double hz, double& outI, double& outQ) {
    Dds reference(hz, kSampleRateHz);
    double i = 0.0, q = 0.0;
    for (Sample s : samples) {
        Sample cosine;
        const Sample sine = reference.NextSample(cosine);
        i += static_cast<double>(s) * static_cast<double>(cosine);
        q += static_cast<double>(s) * static_cast<double>(sine);
    }
    const double scale = 2.0 / static_cast<double>(samples.size());
    outI = i * scale;
    outQ = q * scale;
}
} // namespace

TEST(LineProbingSignalGenerator_L2_HasEqualMagnitudeAtEveryTableToneFrequency) {
    LineProbingSignalGenerator generator(kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> samples;
    generator.GenerateL2Samples(samples, kL1DurationSamples);

    for (double hz : LineProbingSignalGenerator::kToneFrequenciesHz) {
        const double magnitude = GoertzelMagnitudeAt(samples, hz);
        EXPECT_TRUE(std::abs(magnitude - kExpectedPerToneMagnitude) < 0.02);
    }
}

TEST(LineProbingSignalGenerator_L2_HasNoEnergyAtOmittedFrequencies) {
    // 900, 1200, 1800, 2400 Hz are explicitly omitted by 10.1.2.4.
    LineProbingSignalGenerator generator(kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> samples;
    generator.GenerateL2Samples(samples, kL1DurationSamples);

    const double omitted[] = {900.0, 1200.0, 1800.0, 2400.0};
    for (double hz : omitted) {
        const double magnitude = GoertzelMagnitudeAt(samples, hz);
        EXPECT_TRUE(magnitude < 0.01);
    }
}

TEST(LineProbingSignalGenerator_L1_Is6dBAboveL2) {
    LineProbingSignalGenerator generator(kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> l1Samples;
    generator.GenerateL1Samples(l1Samples, kL1DurationSamples);

    LineProbingSignalGenerator generator2(kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> l2Samples;
    generator2.GenerateL2Samples(l2Samples, kL1DurationSamples);

    const double l1Magnitude = GoertzelMagnitudeAt(l1Samples, 150.0);
    const double l2Magnitude = GoertzelMagnitudeAt(l2Samples, 150.0);

    const double ratio = l1Magnitude / l2Magnitude;
    EXPECT_TRUE(ratio > 1.9 && ratio < 2.1); // 10^(6/20) ~= 1.995
}

// Confirms the sin-basis-to-cosine-basis fix (the +90-degree AddPhase
// every tone carries, see LineProbingSignalGenerator.h) actually took
// effect: a correctly cosine-phased pure tone over an integer number
// of cycles should correlate almost entirely into the cosine-aligned
// (I) component, not the sine-aligned (Q) one.
TEST(LineProbingSignalGenerator_TonesAreCosinePhased_NotSinePhased) {
    LineProbingSignalGenerator generator(kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> samples;
    generator.GenerateL2Samples(samples, kL1DurationSamples);

    double i150, q150;
    CorrelateCosine(samples, 150.0, i150, q150); // Table 17: 0 degrees
    EXPECT_TRUE(i150 > 0.1);
    EXPECT_TRUE(std::abs(q150) < 0.02);

    double i300, q300;
    CorrelateCosine(samples, 300.0, i300, q300); // Table 17: 180 degrees
    EXPECT_TRUE(i300 < -0.1);
    EXPECT_TRUE(std::abs(q300) < 0.02);
}

TEST(LineProbingSignalGenerator_L1ThenL2_TonePhaseContinuesWithNoDiscontinuity) {
    // L1 and L2 are "the same" periodic signal per 10.1.2.4, differing
    // only in level -- confirm no phase reset happens at the L1-to-L2
    // boundary by checking the 150 Hz tone's phase is still
    // cosine-aligned (not shifted) immediately after the switch.
    LineProbingSignalGenerator generator(kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> l1;
    generator.GenerateL1Samples(l1, kL1DurationSamples);
    std::vector<Sample> l2;
    generator.GenerateL2Samples(l2, kL1DurationSamples);

    double i, q;
    CorrelateCosine(l2, 150.0, i, q);
    EXPECT_TRUE(i > 0.1);
    EXPECT_TRUE(std::abs(q) < 0.02);
}

TEST(LineProbingSignalGenerator_Reset_RestoresTable17InitialPhases) {
    LineProbingSignalGenerator generator(kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> discard;
    generator.GenerateL2Samples(discard, 777); // run for an arbitrary, non-cycle-aligned length
    generator.Reset();

    std::vector<Sample> samples;
    generator.GenerateL2Samples(samples, kL1DurationSamples);

    double i150, q150;
    CorrelateCosine(samples, 150.0, i150, q150);
    EXPECT_TRUE(i150 > 0.1);
    EXPECT_TRUE(std::abs(q150) < 0.02);

    double i300, q300;
    CorrelateCosine(samples, 300.0, i300, q300);
    EXPECT_TRUE(i300 < -0.1);
}
