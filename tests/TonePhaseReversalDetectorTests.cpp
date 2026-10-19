#include "../src/Lib/Dsp/TonePhaseReversalDetector.h"
#include "../src/Lib/Dsp/V34ProbingTones.h"
#include "TestFramework.h"

#include <algorithm>
#include <cmath>

using namespace voipmodem::dsp;

namespace {
constexpr Sample kNominalAmplitude = 0.8f;
constexpr int kWindowSamples = 40; // matches the detector's own default

// How far (in samples) a detected reversal is allowed to land from the
// true sample index at which it was actually inserted. The detector's
// own class comment explains why this can't be exact: the sign flip
// only registers once more than half the sliding window's energy is
// past the transition, so +-windowSamples/2 is the expected -- not
// generous -- tolerance, not a loosened one.
constexpr long long kTimingTolerance = kWindowSamples / 2 + 2;
} // namespace

TEST(TonePhaseReversalDetector_NoReversal_ReportsNoneOverALongTone) {
    ToneWithPhaseReversal tone(2400.0, kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> samples;
    tone.GenerateSamples(samples, 2000); // 250 ms, well past the window fill

    TonePhaseReversalDetector detector(2400.0, kSampleRateHz, kWindowSamples);
    const std::vector<long long> reversals = detector.ProcessSamples(samples);

    EXPECT_EQ(reversals.size(), static_cast<size_t>(0));
}

TEST(TonePhaseReversalDetector_SingleReversal_DetectedNearTrueSampleIndex) {
    ToneWithPhaseReversal tone(2400.0, kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> samples;
    const int preReversalSamples = 400; // enough for the baseline window to settle
    tone.GenerateSamples(samples, preReversalSamples);
    const long long trueReversalIndex = static_cast<long long>(samples.size());
    tone.ReversePhase();
    tone.GenerateSamples(samples, 400);

    TonePhaseReversalDetector detector(2400.0, kSampleRateHz, kWindowSamples);
    const std::vector<long long> reversals = detector.ProcessSamples(samples);

    EXPECT_EQ(reversals.size(), static_cast<size_t>(1));
    if (!reversals.empty()) {
        EXPECT_TRUE(std::llabs(reversals[0] - trueReversalIndex) <= kTimingTolerance);
    }
}

TEST(TonePhaseReversalDetector_TwoReversals_BothDetectedNearTrueSampleIndices) {
    ToneWithPhaseReversal tone(1200.0, kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> samples;
    tone.GenerateSamples(samples, 400);
    const long long firstReversalIndex = static_cast<long long>(samples.size());
    tone.ReversePhase();
    tone.GenerateSamples(samples, 400);
    const long long secondReversalIndex = static_cast<long long>(samples.size());
    tone.ReversePhase();
    tone.GenerateSamples(samples, 400);

    TonePhaseReversalDetector detector(1200.0, kSampleRateHz, kWindowSamples);
    const std::vector<long long> reversals = detector.ProcessSamples(samples);

    EXPECT_EQ(reversals.size(), static_cast<size_t>(2));
    if (reversals.size() == 2) {
        EXPECT_TRUE(std::llabs(reversals[0] - firstReversalIndex) <= kTimingTolerance);
        EXPECT_TRUE(std::llabs(reversals[1] - secondReversalIndex) <= kTimingTolerance);
    }
}

// Real call audio arrives frame-by-frame, not as one contiguous buffer
// -- same discipline as Dbpsk_ChunkedDelivery_MatchesWholeBufferDecoding
// and Fsk_ChunkedDelivery_MatchesWholeBufferDecoding. The detector
// carries all of its sliding-window state internally across calls, so
// feeding it in small frames must report exactly the same reversals as
// feeding it everything at once.
TEST(TonePhaseReversalDetector_ChunkedDelivery_MatchesWholeBufferDetection) {
    ToneWithPhaseReversal tone(2400.0, kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> allSamples;
    tone.GenerateSamples(allSamples, 400);
    tone.ReversePhase();
    tone.GenerateSamples(allSamples, 400);

    TonePhaseReversalDetector wholeBufferDetector(2400.0, kSampleRateHz, kWindowSamples);
    const std::vector<long long> expected = wholeBufferDetector.ProcessSamples(allSamples);

    TonePhaseReversalDetector chunkedDetector(2400.0, kSampleRateHz, kWindowSamples);
    std::vector<long long> chunked;
    const size_t kFrameSize = 160; // matches an 8kHz/20ms RTP frame
    for (size_t offset = 0; offset < allSamples.size(); offset += kFrameSize) {
        const size_t end = std::min(offset + kFrameSize, allSamples.size());
        const std::vector<Sample> chunk(allSamples.begin() + static_cast<long>(offset),
                                         allSamples.begin() + static_cast<long>(end));
        const std::vector<long long> newReversals = chunkedDetector.ProcessSamples(chunk);
        chunked.insert(chunked.end(), newReversals.begin(), newReversals.end());
    }

    EXPECT_EQ(chunked.size(), expected.size());
    for (size_t i = 0; i < expected.size() && i < chunked.size(); ++i) {
        EXPECT_EQ(chunked[i], expected[i]);
    }
}

TEST(TonePhaseReversalDetector_Silence_ReportsNoReversals) {
    const std::vector<Sample> silence(2000, 0.0f);

    TonePhaseReversalDetector detector(2400.0, kSampleRateHz, kWindowSamples);
    const std::vector<long long> reversals = detector.ProcessSamples(silence);

    EXPECT_EQ(reversals.size(), static_cast<size_t>(0));
}

// Tone A's own 1800 Hz guard tone (ToneAGenerator, V34ProbingTones.h)
// is additively mixed into the same audio a real Tone A detector would
// have to pick a reversal out of -- confirm the 2400 Hz-targeted
// detector isn't thrown off by that extra, never-reversing energy at a
// different frequency.
TEST(TonePhaseReversalDetector_IgnoresToneAsGuardTone) {
    ToneAGenerator generator(kNominalAmplitude, kSampleRateHz);
    std::vector<Sample> samples;
    generator.GenerateSamples(samples, 400);
    const long long trueReversalIndex = static_cast<long long>(samples.size());
    generator.ReversePhase();
    generator.GenerateSamples(samples, 400);

    TonePhaseReversalDetector detector(2400.0, kSampleRateHz, kWindowSamples);
    const std::vector<long long> reversals = detector.ProcessSamples(samples);

    EXPECT_EQ(reversals.size(), static_cast<size_t>(1));
    if (!reversals.empty()) {
        EXPECT_TRUE(std::llabs(reversals[0] - trueReversalIndex) <= kTimingTolerance);
    }
}
