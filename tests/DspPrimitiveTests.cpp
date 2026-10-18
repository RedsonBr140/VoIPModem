#include "../src/Lib/Dsp/Dds.h"
#include "../src/Lib/Dsp/GoertzelDetector.h"
#include "../src/Lib/Dsp/ToneGenerator.h"
#include "TestFramework.h"

#include <cmath>

using namespace voipmodem::dsp;

namespace {

bool NearlyEqual(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

} // namespace

// At 2000 Hz into an 8000 Hz sample rate, the phase advances exactly pi/2
// per sample -- a case with exact, easily-verified expected values, so
// this catches a wrong phase-increment formula or a wrap-direction bug
// without relying on looser floating-point tolerances everywhere else.
TEST(Dds_QuarterSampleRateTone_ProducesExactQuadratureSequence) {
    Dds dds(2000.0, 8000);
    EXPECT_TRUE(NearlyEqual(dds.NextSample(), 0.0, 1e-6));
    EXPECT_TRUE(NearlyEqual(dds.NextSample(), 1.0, 1e-6));
    EXPECT_TRUE(NearlyEqual(dds.NextSample(), 0.0, 1e-6));
    EXPECT_TRUE(NearlyEqual(dds.NextSample(), -1.0, 1e-6));
}

TEST(Dds_Reset_RewindsPhaseWithoutChangingFrequency) {
    Dds dds(2000.0, 8000);
    dds.NextSample();
    dds.NextSample();
    dds.Reset();
    EXPECT_TRUE(NearlyEqual(dds.NextSample(), 0.0, 1e-6));
}

TEST(Dds_AddPhase_JumpsWithoutDisturbingSubsequentFrequency) {
    Dds dds(2000.0, 8000);
    dds.NextSample(); // phase now at pi/2
    dds.AddPhase(3.14159265358979323846); // +pi: a differential-PSK-style 180-degree symbol flip
    // Continuing from pi/2 + pi = 3pi/2: sin(3pi/2) = -1, then same
    // quarter-sample-rate quadrature sequence as before, just phase-shifted.
    EXPECT_TRUE(NearlyEqual(dds.NextSample(), -1.0, 1e-6));
    EXPECT_TRUE(NearlyEqual(dds.NextSample(), 0.0, 1e-6));
    EXPECT_TRUE(NearlyEqual(dds.NextSample(), 1.0, 1e-6));
}

TEST(Dds_AddPhase_WrapsNegativeAndOverlyLargeAngles) {
    Dds dds(2000.0, 8000);
    dds.AddPhase(-3.14159265358979323846); // -pi, from phase 0 -- should wrap to a valid [0, 2pi) phase
    EXPECT_TRUE(NearlyEqual(dds.NextSample(), 0.0, 1e-6)); // sin(-pi) == sin(pi) == 0
    EXPECT_TRUE(NearlyEqual(dds.NextSample(), -1.0, 1e-6));
}

TEST(GoertzelDetector_ToneAtTargetFrequency_ReadsFullAmplitude) {
    const double frequencyHz = 2100.0; // V.25 answer tone (ANS)
    const int blockSize = 160;         // 20ms at 8kHz

    ToneGenerator generator(frequencyHz, 1.0f, kSampleRateHz);
    std::vector<Sample> samples;
    generator.Generate(samples, blockSize);

    GoertzelDetector detector(frequencyHz, kSampleRateHz);
    for (Sample s : samples) {
        detector.Process(s);
    }

    EXPECT_TRUE(NearlyEqual(detector.Magnitude(blockSize), 1.0, 0.05));
}

TEST(GoertzelDetector_ToneAtOtherFrequency_ReadsNearZero) {
    const int blockSize = 160;

    ToneGenerator generator(2100.0, 1.0f, kSampleRateHz); // generate ANS...
    std::vector<Sample> samples;
    generator.Generate(samples, blockSize);

    GoertzelDetector detector(1000.0, kSampleRateHz); // ...but listen elsewhere
    for (Sample s : samples) {
        detector.Process(s);
    }

    EXPECT_TRUE(detector.Magnitude(blockSize) < 0.1);
}

TEST(GoertzelDetector_Silence_ReadsZero) {
    const int blockSize = 160;
    GoertzelDetector detector(2100.0, kSampleRateHz);
    for (int i = 0; i < blockSize; ++i) {
        detector.Process(0.0f);
    }
    EXPECT_TRUE(NearlyEqual(detector.Magnitude(blockSize), 0.0, 1e-6));
}

TEST(GoertzelDetector_Reset_ClearsAccumulatedEnergy) {
    const int blockSize = 160;
    ToneGenerator generator(2100.0, 1.0f, kSampleRateHz);
    std::vector<Sample> samples;
    generator.Generate(samples, blockSize);

    GoertzelDetector detector(2100.0, kSampleRateHz);
    for (Sample s : samples) {
        detector.Process(s);
    }
    detector.Reset();

    EXPECT_TRUE(NearlyEqual(detector.Magnitude(blockSize), 0.0, 1e-6));
}
