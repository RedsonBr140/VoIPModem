#pragma once

#include "DspTypes.h"

namespace voipmodem::dsp {

// Direct digital synthesis oscillator: produces successive samples of a
// sine/cosine pair from a wrapped phase accumulator, rather than computing
// sin(2*pi*f*t) from an ever-growing absolute time (which loses precision
// over a long call). This is the shared carrier/tone-generation primitive
// everything above it (ToneGenerator now; QAM carrier modulation, V.34
// probe tones, etc. later) builds on.
class Dds {
public:
    explicit Dds(double frequencyHz, int sampleRateHz = kSampleRateHz);

    void SetFrequency(double frequencyHz);

    // Advances the oscillator by one sample and returns sin(phase).
    Sample NextSample();

    // Same, but also writes cos(phase) to `outCosine` -- the quadrature
    // component QAM modulation needs for its I/Q carrier; a plain tone
    // generator has no use for it.
    Sample NextSample(Sample& outCosine);

    // Rewinds the phase accumulator to zero without touching the
    // configured frequency.
    void Reset();

private:
    int sampleRateHz_;
    double phaseIncrement_ = 0.0; // radians per sample
    double phase_ = 0.0;          // radians, wrapped to [0, 2*pi)
};

} // namespace voipmodem::dsp
