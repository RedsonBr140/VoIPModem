#pragma once

#include "DspTypes.h"

#include <utility>
#include <vector>

namespace voipmodem::dsp {

// The receive-side counterpart to V34QamModulator: carrier
// downconversion + SRRC matched filtering, sampled at the symbol rate,
// turning a passband waveform back into a sequence of (I, Q) estimates.
//
// IMPORTANT, read before trusting or extending this -- this is
// deliberately NOT a real V.34 receiver, for the same reason
// V34QamModulator is not yet wired into a real call: it does no
// carrier-phase recovery and no symbol-timing recovery. It assumes the
// caller already knows both exactly -- specifically, that sample 0 of
// `waveform` sits exactly `filterSpanSymbols` symbol periods before
// symbol 0's center, the same convention V34QamModulator::Modulate
// uses for its own output. That is true in a lossless loopback test
// (modulate, then immediately demodulate the exact same buffer) but
// would never be true on a real link, where the far end's carrier
// phase and symbol clock are unknown and drifting. A real V.34 receiver
// needs its own carrier recovery (akin to what V8Handshake eventually
// needed for FSK, clause 9.6) and timing recovery (akin to
// FskDemodulator's early-late gate, clause 9.5/equalizer) before this
// class's approach could run on real call audio -- neither exists yet.
//
// What this DOES verify, and why that is still worth having before any
// of that exists: that the downconversion + SRRC-matched-filter
// arithmetic itself is correct -- i.e. that V34QamModulator's output,
// fed through the textbook-matched receive filter, actually recovers
// the original symbol sequence (up to one unknown real-valued scale
// factor; see Demodulate()'s own comment for why no phase ambiguity
// also arises here). That is a meaningful, independently-testable
// building block the same way Dds/GoertzelDetector were verified in
// isolation before FskModulator/FskDemodulator combined them, and the
// same way V8Handshake's own loopback test (TX fed directly into RX,
// no real audio path) was run before any live-call test was attempted.
//
// KNOWN, MEASURED LIMITATION (not a bug; see
// V34QamDemodulatorTests.cpp's Demodulate_AtActualV34SymbolRate_...
// test for the measurement): this class's direct closed-form-pulse
// matched filter only works well when the sample rate comfortably
// oversamples the symbol rate. At V.34's own actual operating point --
// 2400 baud over this codebase's fixed 8 kHz sample rate (kSampleRateHz,
// DspTypes.h), i.e. only ~3.33 samples per symbol -- the discrete sum
// used here is too coarse a quadrature of the true matched-filter
// integral, and produces severe residual ISI (~76% of symbols decoded
// to the wrong constellation point on a real 16-point TRN burst, as
// measured). This was confirmed to be a quadrature/discretization
// problem, not an aliasing problem (the signal itself sits well inside
// the 8 kHz Nyquist bandwidth) and not fixed by a wider
// filterSpanSymbols (tried 4x the default span; barely moved the
// error rate) -- the same code decodes perfectly at a lower baud rate
// with more samples/symbol. A real fix needs either a properly
// designed discrete-time (fractionally-spaced) matched filter instead
// of this direct closed-form evaluation, or an interpolation/
// upsampling stage ahead of it; neither exists yet. Do not wire this
// into CallAudioBridge or claim it can decode real V.34-rate symbols
// until one of those is built and re-measured.
class V34QamDemodulator {
public:
    // Parameters must match the V34QamModulator that produced
    // `waveform` exactly (symbol rate, carrier frequency, roll-off,
    // filter span) -- this class does not infer any of them.
    V34QamDemodulator(double symbolRateHz, double carrierFrequencyHz, double rolloffFactor = 0.125,
                       int filterSpanSymbols = 8);

    // Recovers `numSymbols` (I, Q) estimates from `waveform`, assuming
    // the same sample-0-to-symbol-0 timing and absolute carrier-phase
    // reference V34QamModulator::Modulate used to produce it (see class
    // comment). The returned values are proportional to the original
    // transmit constellation points but at an unknown real-valued scale
    // (a function of the transmit amplitude, the carrier downconversion,
    // and the matched filter's self-convolution energy) -- callers that
    // need actual constellation decisions must estimate that scale
    // themselves (e.g. via a handful of known training symbols), the
    // same way a real equalizer would from TRN. No phase/quadrature
    // ambiguity is introduced by this class itself, because it reuses
    // the exact same absolute carrier-phase reference
    // (cos/sin(2*pi*carrierFrequencyHz*n/kSampleRateHz) for sample
    // index n) that Modulate() used -- a real receiver would not have
    // this for free.
    std::vector<std::pair<double, double>> Demodulate(const std::vector<Sample>& waveform, int numSymbols) const;

private:
    double symbolRateHz_;
    double carrierFrequencyHz_;
    double rolloffFactor_;
    int filterSpanSymbols_;
};

} // namespace voipmodem::dsp
