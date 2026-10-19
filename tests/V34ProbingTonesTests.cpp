#include "../src/Lib/Dsp/V34ProbingTones.h"
#include "../src/Lib/Dsp/Dds.h"
#include "../src/Lib/Dsp/GoertzelDetector.h"
#include "TestFramework.h"

#include <cmath>

using namespace voipmodem::dsp;

namespace {
constexpr Sample kNominalAmplitude = 0.8f;

// Correlates `block` (starting at `offset`, `blockLength` samples) against
// a local reference carrier at `hz`, returning normalized (I, Q). Used
// below to confirm a ReversePhase() call actually flips the carrier's
// phase by ~180 degrees -- the same differential-comparison idea
// DbpskDemodulator uses, just applied here directly in the test rather
// than through a not-yet-built detector class (see V34ProbingTones.h's
// class comment: detection is deliberately out of scope for this
// increment).
void Correlate(const std::vector<Sample>& block, size_t offset, size_t blockLength, double hz, int sampleRateHz,
                double& outI, double& outQ) {
    Dds reference(hz, sampleRateHz);
    double i = 0.0, q = 0.0;
    for (size_t n = 0; n < blockLength; ++n) {
        Sample cosine;
        const Sample sine = reference.NextSample(cosine);
        i += static_cast<double>(block[offset + n]) * static_cast<double>(cosine);
        q += static_cast<double>(block[offset + n]) * static_cast<double>(sine);
    }
    outI = i;
    outQ = q;
}
} // namespace

TEST(ToneWithPhaseReversal_NoReversal_StaysAtConstantFrequencyAndAmplitude) {
    ToneWithPhaseReversal tone(2400.0, kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> samples;
    tone.GenerateSamples(samples, 800); // 100 ms

    GoertzelDetector detector(2400.0, kSampleRateHz);
    for (Sample s : samples) {
        detector.Process(s);
    }
    const double magnitude = detector.Magnitude(static_cast<int>(samples.size()));
    EXPECT_TRUE(magnitude > 0.7 && magnitude < 0.9);
}

TEST(ToneWithPhaseReversal_ReversePhase_FlipsCorrelationSign) {
    ToneWithPhaseReversal tone(2400.0, kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> samples;
    const int blockLength = 80; // several full cycles at 2400 Hz/8kHz
    tone.GenerateSamples(samples, blockLength);
    tone.ReversePhase();
    tone.GenerateSamples(samples, blockLength);

    double i1, q1, i2, q2;
    Correlate(samples, 0, blockLength, 2400.0, kSampleRateHz, i1, q1);
    Correlate(samples, static_cast<size_t>(blockLength), blockLength, 2400.0, kSampleRateHz, i2, q2);

    // Same differential test DbpskDemodulator itself relies on: a true
    // 180-degree reversal makes this dot product negative.
    const double dot = i1 * i2 + q1 * q2;
    EXPECT_TRUE(dot < 0.0);
}

TEST(ToneWithPhaseReversal_TwoReversals_ReturnsToOriginalPhase) {
    ToneWithPhaseReversal tone(2400.0, kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> samples;
    const int blockLength = 80;
    tone.GenerateSamples(samples, blockLength);
    tone.ReversePhase();
    tone.ReversePhase(); // back to the original phase (360 degrees total)
    tone.GenerateSamples(samples, blockLength);

    double i1, q1, i2, q2;
    Correlate(samples, 0, blockLength, 2400.0, kSampleRateHz, i1, q1);
    Correlate(samples, static_cast<size_t>(blockLength), blockLength, 2400.0, kSampleRateHz, i2, q2);

    const double dot = i1 * i2 + q1 * q2;
    EXPECT_TRUE(dot > 0.0);
}

TEST(ToneAGenerator_ProducesToneOneDbBelowNominalGuardTone) {
    // 10.1.2.1: Tone A (2400 Hz) is 1 dB below nominal; the 1800 Hz guard
    // tone runs at the full nominal power the whole time.
    ToneAGenerator generator(kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> samples;
    generator.GenerateSamples(samples, 1600); // 200 ms, several Goertzel windows' worth

    GoertzelDetector toneDetector(2400.0, kSampleRateHz);
    GoertzelDetector guardDetector(1800.0, kSampleRateHz);
    for (Sample s : samples) {
        toneDetector.Process(s);
        guardDetector.Process(s);
    }
    const double toneMag = toneDetector.Magnitude(static_cast<int>(samples.size()));
    const double guardMag = guardDetector.Magnitude(static_cast<int>(samples.size()));

    // Guard tone should read close to the full nominal amplitude...
    EXPECT_TRUE(guardMag > 0.7 && guardMag < 0.9);
    // ...and Tone A itself a bit quieter (1 dB down is a ratio of ~0.891).
    EXPECT_TRUE(toneMag < guardMag);
    const double ratio = toneMag / guardMag;
    EXPECT_TRUE(ratio > 0.80 && ratio < 0.95);
}

TEST(ToneAGenerator_ReversePhase_OnlyAffectsTheToneNotTheGuardTone) {
    ToneAGenerator generator(kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> samples;
    const int blockLength = 80;
    generator.GenerateSamples(samples, blockLength);
    generator.ReversePhase();
    generator.GenerateSamples(samples, blockLength);

    double toneI1, toneQ1, toneI2, toneQ2;
    Correlate(samples, 0, blockLength, 2400.0, kSampleRateHz, toneI1, toneQ1);
    Correlate(samples, static_cast<size_t>(blockLength), blockLength, 2400.0, kSampleRateHz, toneI2, toneQ2);
    EXPECT_TRUE(toneI1 * toneI2 + toneQ1 * toneQ2 < 0.0);

    double guardI1, guardQ1, guardI2, guardQ2;
    Correlate(samples, 0, blockLength, 1800.0, kSampleRateHz, guardI1, guardQ1);
    Correlate(samples, static_cast<size_t>(blockLength), blockLength, 1800.0, kSampleRateHz, guardI2, guardQ2);
    EXPECT_TRUE(guardI1 * guardI2 + guardQ1 * guardQ2 > 0.0);
}

TEST(ToneBGenerator_HasNoGuardTone) {
    // Unlike Tone A, 10.1.2.2's text for Tone B specifies no guard tone.
    ToneBGenerator generator(kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> samples;
    generator.GenerateSamples(samples, 1600);

    GoertzelDetector toneDetector(1200.0, kSampleRateHz);
    GoertzelDetector guardDetector(1800.0, kSampleRateHz);
    for (Sample s : samples) {
        toneDetector.Process(s);
        guardDetector.Process(s);
    }
    const double toneMag = toneDetector.Magnitude(static_cast<int>(samples.size()));
    const double guardMag = guardDetector.Magnitude(static_cast<int>(samples.size()));

    EXPECT_TRUE(toneMag > 0.7 && toneMag < 0.9);
    EXPECT_TRUE(guardMag < 0.05); // no 1800 Hz content at all
}

TEST(ToneBGenerator_ReversePhase_FlipsCorrelationSign) {
    ToneBGenerator generator(kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> samples;
    const int blockLength = 160; // several cycles at 1200 Hz/8kHz
    generator.GenerateSamples(samples, blockLength);
    generator.ReversePhase();
    generator.GenerateSamples(samples, blockLength);

    double i1, q1, i2, q2;
    Correlate(samples, 0, blockLength, 1200.0, kSampleRateHz, i1, q1);
    Correlate(samples, static_cast<size_t>(blockLength), blockLength, 1200.0, kSampleRateHz, i2, q2);

    EXPECT_TRUE(i1 * i2 + q1 * q2 < 0.0);
}
