#pragma once

#include "DspTypes.h"

namespace voipmodem::dsp {

// Detects the energy present at one specific frequency in a block of
// samples, using the Goertzel algorithm -- a single-bin DFT, far cheaper
// than a full FFT when only one or two target frequencies matter. That is
// exactly the handshake-tone case: V.25 answer tone, calling tone, ANSam,
// and (later) V.34's probe tones each need detecting at one known
// frequency, not a full spectrum.
//
// This only estimates magnitude at the target frequency; deciding whether
// that magnitude means "a real tone is present" (vs. noise, or a human
// voice) is the caller's job -- a threshold against measured signal
// power, not something this class can know on its own.
class GoertzelDetector {
public:
    explicit GoertzelDetector(double targetFrequencyHz, int sampleRateHz = kSampleRateHz);

    // Resets internal accumulators. Call before feeding a new block.
    void Reset();

    // Feeds one sample into the running Goertzel recursion.
    void Process(Sample sample);

    // Returns the estimated amplitude of the target-frequency component
    // across the `blockSize` samples fed in since the last Reset() --
    // normalized so a full-scale pure tone at the target frequency reads
    // approximately 1.0, the same scale as the sample amplitudes going
    // in (not a raw, unnormalized DFT bin magnitude).
    double Magnitude(int blockSize) const;

private:
    double coefficient_;
    double q1_ = 0.0;
    double q2_ = 0.0;
};

} // namespace voipmodem::dsp
