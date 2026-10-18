#pragma once

#include "Dds.h"
#include "DspTypes.h"
#include "GoertzelDetector.h"

namespace voipmodem::dsp {

// ITU-T V.8 7.2 -- the modified answer tone ANSam: a 2100 Hz sinewave,
// amplitude-modulated by a 15 Hz sinewave (envelope ranging 0.8x-1.2x
// of average amplitude, i.e. average 1.0 +/- 0.2), with phase reversals
// every 450 +/- 25 ms (for network echo-canceller disabling). A phase
// reversal of a pure sinewave is just negating it (sin(x+pi) ==
// -sin(x)), so that's how this generator implements it -- no separate
// phase-tracking needed.
//
// Honestly simplified: the exact reversal interval is fixed at 450 ms
// (the spec's own center value, not randomized within its +/-25ms
// tolerance -- nothing in this codebase needs that tolerance modeled).
class AnsamGenerator {
public:
    explicit AnsamGenerator(float amplitude = 0.6f, int sampleRateHz = kSampleRateHz);
    Sample NextSample();
    void Reset();

private:
    int sampleRateHz_;
    float amplitude_;
    Dds carrier_;   // 2100 Hz
    Dds envelope_;  // 15 Hz
    int samplesSincePhaseReversal_ = 0;
    int samplesPerPhaseReversal_;
    bool phaseInverted_ = false;
};

// Thin GoertzelDetector wrapper fixed at ANSam's 2100 Hz carrier, named
// for what it's actually detecting rather than leaving the frequency
// as an unlabeled constant at every call site. Does not distinguish
// ANSam from plain, unmodulated ANS (7.2's own note flags that a real
// detector needs to); nothing in this codebase has needed that
// distinction yet.
class AnsamDetector {
public:
    explicit AnsamDetector(int sampleRateHz = kSampleRateHz);
    void Process(Sample sample);
    double Magnitude(int blockSize) const;
    void Reset();

private:
    GoertzelDetector detector_;
};

} // namespace voipmodem::dsp
