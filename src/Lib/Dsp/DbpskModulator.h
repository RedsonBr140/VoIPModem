#pragma once

#include <vector>

#include "Dds.h"
#include "DspTypes.h"

namespace voipmodem::dsp {

// Differential binary phase-shift-keying modulator, per ITU-T V.34
// clause 10.1.2.3.1 -- the modulation INFO0/INFO1/INFOMARKS sequences
// actually use (confirmed against the real Recommendation text, same
// `pdftotext` approach as V.8/the rest of V.34 -- see
// docs/ROADMAP.md Phase 4). Unlike everything built so far in this
// module (FSK: switch *frequency* per bit), DBPSK keeps the carrier
// frequency fixed the whole time and instead rotates its *phase* by
// 180 degrees for a 1 bit, or leaves it unrotated for a 0 bit --
// "differential" because each symbol only encodes a *change* relative
// to the previous symbol, not an absolute phase, which is what makes
// DbpskDemodulator robust to not knowing the channel's absolute phase
// offset.
//
// Deliberately scoped to just the modulation technique in isolation,
// the same discipline used for FskModulator/FskDemodulator before
// V8Message/V8CallMenu/V8Signal/V8Handshake existed on top of them:
// this class knows nothing about INFO0's bit layout (V34Info0), the
// 2400Hz/1200Hz carrier choice or guard tones V.34 actually specifies
// for the answer/call modem (10.1.2.3.1 -- a guard tone is a separate,
// additively-mixed continuous tone, not this class's job, same
// separation ToneGenerator/AnsamTone already keep from FskModulator),
// or sequencing/timing. Not yet wired into anything.
class DbpskModulator {
public:
    DbpskModulator(double carrierHz, double baudRate, Sample amplitude, int sampleRateHz = kSampleRateHz);

    // Appends one symbol period's worth of samples to `out`, rotating
    // the carrier's phase by 180 degrees relative to the *previous*
    // symbol if `bit` is true, or leaving it unrotated if `bit` is
    // false -- 10.1.2.3.1's exact rule. The exact sample count varies
    // by +-1 across calls when sampleRateHz/baudRate isn't a whole
    // number (600 baud at 8kHz: 13.33 samples/symbol), using the same
    // fractional-remainder technique as FskModulator/Dds, so the
    // *average* symbol rate over many symbols is exact.
    void ModulateBit(std::vector<Sample>& out, bool bit);

    // 10.1.2.3.1: "Each INFO sequence is preceded by a point at an
    // arbitrary carrier phase." Appends one symbol period at the
    // carrier's current phase, with no rotation -- establishing the
    // reference point DbpskDemodulator's *first* differential
    // comparison needs, without itself carrying a data bit. Only the
    // first sequence in a transmitted group needs this (10.1.2.3.1);
    // back-to-back sequences share one continuous carrier and need no
    // extra reference point between them, since each sequence's last
    // symbol already serves as the next sequence's reference.
    void EmitReferenceSymbol(std::vector<Sample>& out);

    // Rewinds phase and the fractional symbol-length remainder. Does
    // not change the configured frequency/baud rate.
    void Reset();

private:
    void EmitSymbol(std::vector<Sample>& out, bool rotate);

    Dds dds_;
    Sample amplitude_;
    double exactSamplesPerSymbol_;
    double samplesPerSymbolRemainder_ = 0.0;
};

} // namespace voipmodem::dsp
