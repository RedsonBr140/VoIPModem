#pragma once

#include <vector>

#include "Dds.h"
#include "DspTypes.h"

namespace voipmodem::dsp {

// DBPSK demodulator counterpart to DbpskModulator (see its header for
// the modulation rule, confirmed against ITU-T V.34 clause 10.1.2.3.1).
//
// Correlates each symbol's samples against a continuously-running local
// reference carrier (an internal Dds, never reset mid-stream) to get
// that symbol's in-phase/quadrature components (I, Q) -- the same
// single-frequency-bin idea as GoertzelDetector, but keeping both
// components instead of collapsing them into one magnitude, because
// phase (not amplitude) is what carries the bit here. The bit is then
// decided *differentially*: the real part of this symbol's (I, Q)
// times the complex conjugate of the *previous* symbol's (I, Q) is
// negative exactly when the phase rotated ~180 degrees between them.
// This is deliberately robust to not knowing the channel's absolute
// phase offset -- it only ever compares consecutive symbols to each
// other, never to an assumed absolute reference -- which is the whole
// point of using a *differential* PSK scheme here rather than plain
// BPSK.
//
// The very first symbol fed in produces no bit: per 10.1.2.3.1, an
// INFO sequence's first symbol is "a point at an arbitrary carrier
// phase" with no data meaning of its own, only establishing the
// baseline the second symbol's rotation (or lack of it) is measured
// against -- matching DbpskModulator::EmitReferenceSymbol on the
// transmit side.
//
// Streaming by design, same discipline as FskDemodulator: carries all
// symbol-boundary state as members across calls, so a caller can feed
// any number of new samples at a time without an ever-growing buffer.
// Still, honestly, missing (same caveat as FskDemodulator before this
// session's early-late gate): no symbol-timing recovery of its own yet
// -- this is a new DSP primitive, not a reuse of FskDemodulator's, so
// it doesn't automatically inherit that fix. Not yet wired into
// anything.
class DbpskDemodulator {
public:
    DbpskDemodulator(double carrierHz, double baudRate, int sampleRateHz = kSampleRateHz);

    // Feeds new samples and returns the bits (zero or more) they
    // completed -- one bit per symbol after the first, which is
    // consumed as the reference point and never produces a bit itself.
    std::vector<bool> ProcessSamples(const std::vector<Sample>& samples);

private:
    Dds dds_; // local reference carrier; intentionally never reset once constructed
    double exactSamplesPerSymbol_;
    double samplesPerSymbolRemainder_ = 0.0;
    int currentSymbolTargetLength_ = -1; // -1 == not yet decided for the symbol in progress
    int samplesIntoCurrentSymbol_ = 0;
    double iAccum_ = 0.0;
    double qAccum_ = 0.0;

    bool hasPreviousSymbol_ = false;
    double previousI_ = 0.0;
    double previousQ_ = 0.0;
};

} // namespace voipmodem::dsp
