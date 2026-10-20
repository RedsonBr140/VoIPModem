#pragma once

#include "DspTypes.h"

#include <utility>
#include <vector>

namespace voipmodem::dsp {

// A QAM pulse-shaping + carrier modulator for V.34 symbol sequences
// (e.g. the TRN training burst from V34Trn.h).
//
// IMPORTANT, read before trusting or extending this: unlike the
// scrambler (clause 7) or the TRN bit mapping (clause 10.1.3.8), the
// actual transmit pulse shape is NOT normatively specified anywhere in
// the Recommendation V.34 (02/98) text. Clause 5.2/5.3 exactly specify
// the symbol rate (S = (a/c)*2400 Hz, Table 1) and carrier frequency
// ((d/e)*S Hz, Table 2) -- those ARE normative and this module takes
// them as exact caller-supplied parameters. But clause 5.4.1 ("Transmit
// spectrum specifications") only gives a power-spectral-density MASK
// with +/-1 dB tolerance (Figures 1/2, Tables 3/4 -- image-based
// graphs, not filter coefficients) that any implementation's transmit
// filter must stay inside; it does not say what filter to use to get
// there. Every real V.34 modem vendor chose its own transmit filter
// design to fit that mask. This module makes the same kind of choice:
// a standard square-root-raised-cosine (SRRC) pulse, which is the
// conventional choice for exactly this kind of mask-constrained QAM
// line coding, with a caller-supplied roll-off factor and filter span.
//
// This module's output has NOT been measured against the Figure 1/2
// spectral template -- doing that honestly would require digitizing
// those mask curves (the same image-extraction problem Figure 5 was --
// see V34Constellation.h) and comparing measured PSD against them.
// Until that happens, do not claim this filter is V.34-conformant,
// only that it is a reasonable, standard choice of the kind the spec
// leaves up to the implementer.
//
// This also only modulates a FIXED, already-known symbol sequence in
// one batch call -- there is no streaming/incremental API yet, and
// no wiring into V34Phase2Handshake or CallAudioBridge. See
// docs/ROADMAP.md for what is still needed before any of this can
// appear on a real call.

// Continuous-time SRRC pulse, normalized so the peak (at t = 0) is
// 1.0 -- i.e. this returns h(t) / h(0) of the standard closed-form SRRC
// impulse response, not the raw (unnormalized) textbook formula. beta
// is the roll-off factor (0, 1]; symbolPeriodSeconds is T = 1/S.
// Exposed for direct unit testing (symmetry, peak location, the
// removable singularity at t = +/- T/(4*beta)) independent of the
// batch Modulate() path.
double SquareRootRaisedCosinePulse(double t, double symbolPeriodSeconds, double beta);

class V34QamModulator {
public:
    // filterSpanSymbols controls how many symbol periods of pulse tail
    // are summed on each side of a given output sample -- a standard
    // FIR truncation of the (infinite-support) SRRC pulse; larger
    // values trade more compute for a closer approximation to the
    // ideal continuous-time filter.
    V34QamModulator(double symbolRateHz, double carrierFrequencyHz, double rolloffFactor = 0.125,
                     int filterSpanSymbols = 8);

    // Modulates an entire symbol sequence (2D constellation points, in
    // Figure 5's raw integer grid units -- see V34Constellation.h) into
    // a real passband waveform at kSampleRateHz. `amplitude` scales
    // each symbol's contribution; callers are responsible for choosing
    // a value that keeps the result within [-1, 1] (the output is not
    // clipped or auto-scaled). A bare SRRC pulse is not zero-ISI (only
    // SRRC matched-filtered with itself at the receiver is), so with a
    // low roll-off many overlapping symbols can sum well past a single
    // symbol's own coordinate magnitude -- empirically around 12x
    // `amplitude` for a 16-point TRN burst at rolloff=0.125, span=8
    // (see V34QamModulatorTests.cpp); that is a rough empirical data
    // point, not a derived bound, so re-check for other parameters.
    // The returned buffer is padded by
    // filterSpanSymbols symbol periods of filter tail on each side, so
    // it is longer than symbols.size() / symbolRateHz * kSampleRateHz.
    std::vector<Sample> Modulate(const std::vector<std::pair<int, int>>& symbols, double amplitude) const;

private:
    double symbolRateHz_;
    double carrierFrequencyHz_;
    double rolloffFactor_;
    int filterSpanSymbols_;
};

} // namespace voipmodem::dsp
