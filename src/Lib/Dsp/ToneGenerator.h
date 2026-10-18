#pragma once

#include <vector>

#include "Dds.h"
#include "DspTypes.h"

namespace voipmodem::dsp {

// Generates a continuous pure tone at a fixed frequency and amplitude --
// the TX-side counterpart to GoertzelDetector. V.25/V.8 handshake tones
// (answer tone, calling tone, ANSam) are all pure tones at a handful of
// known frequencies; this is the one generator all of them share.
class ToneGenerator {
public:
    ToneGenerator(double frequencyHz, Sample amplitude, int sampleRateHz = kSampleRateHz);

    // Appends `count` samples to `out`.
    void Generate(std::vector<Sample>& out, int count);

    void Reset();

private:
    Dds dds_;
    Sample amplitude_;
};

} // namespace voipmodem::dsp
