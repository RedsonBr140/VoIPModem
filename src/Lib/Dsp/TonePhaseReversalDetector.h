#pragma once

#include <deque>
#include <vector>

#include "Dds.h"
#include "DspTypes.h"

namespace voipmodem::dsp {

// Detects 180-degree phase reversals in a continuous single-frequency
// tone -- the piece ToneWithPhaseReversal/ToneAGenerator/ToneBGenerator
// (see V34ProbingTones.h) were missing before round-trip-delay
// measurement (ITU-T V.34 clause 11.2.1.1/11.2.1.2) can mean anything:
// RTDEa/RTDEc are nothing but the time interval between two detected
// reversals, minus a fixed 40ms, so detection timing accuracy directly
// *becomes* RTD accuracy -- unlike a symbol-clocked demodulator
// (DbpskDemodulator), which only needs the right bit, not the right
// instant, and can tolerate being a little late.
//
// Approach: a sliding-window correlator against a local reference
// carrier (never reset, same idea as DbpskDemodulator's), updated
// incrementally one sample at a time rather than in non-overlapping
// blocks, so detection resolution is roughly a sample, not a whole
// window. The window's (I, Q) is compared against a baseline (I, Q)
// captured once the window first fills, and again immediately after
// each detected reversal -- so a *second* reversal is judged against
// the post-first-reversal steady state, not the original phase,
// matching the Recommendation's own "A to A', A' to A" wording, where
// each transition is relative to whatever the immediately preceding
// steady state was, not a fixed absolute reference.
//
// Deliberately scoped to just detecting *that* a reversal happened and
// *when* -- not the 11.2 sequencing/timing/retry procedure that decides
// what to do about it (not yet implemented; see docs/ROADMAP.md).
class TonePhaseReversalDetector {
public:
    // `windowSamples` sets both the correlator's integration time and
    // the detector's dead time after a reversal (so the transition
    // itself, which takes about one window to fully flush through the
    // sliding correlator, can't be mistaken for extra reversals) --
    // long enough for several carrier cycles at the target frequency
    // (needed for a stable phase estimate), short enough to keep
    // reasonable timing resolution. Defaults to 5ms at 8kHz (40
    // samples): ~12 cycles at Tone A's 2400Hz, ~6 at Tone B's 1200Hz.
    explicit TonePhaseReversalDetector(double toneHz, int sampleRateHz = kSampleRateHz, int windowSamples = 40);

    // Feeds new samples; returns the absolute sample index (counted
    // from the first sample ever fed to this detector) of each newly
    // detected reversal in this call, oldest first. A detector that
    // hasn't yet accumulated one full window of samples reports none.
    std::vector<long long> ProcessSamples(const std::vector<Sample>& samples);

private:
    struct Product {
        double i;
        double q;
    };

    Dds referenceDds_; // never reset
    int windowSamples_;
    long long totalSamplesSeen_ = 0;
    std::deque<Product> window_;
    double slidingI_ = 0.0;
    double slidingQ_ = 0.0;

    bool hasBaseline_ = false;
    double baselineI_ = 0.0;
    double baselineQ_ = 0.0;
    long long sampleOfLastReversal_ = -1; // -1 == none yet; gates the dead time
};

} // namespace voipmodem::dsp
