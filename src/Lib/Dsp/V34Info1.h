#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace voipmodem::dsp {

// Bit-level codec for ITU-T V.34's INFO1a/INFO1c sequences (clause
// 10.1.2.3.4/10.1.2.3.5, Tables 15/16) -- confirmed against the real
// Recommendation V.34 (02/98) text, re-read in full for this
// increment, same `pdftotext` approach as the rest of this module's
// V.34 work. Same isolation discipline as `V34Info0`: this file only
// covers the bit layout, not DBPSK transport (see `V34InfoSequence.h`)
// or clause 11.2's sequencing (see `V34Phase2Handshake.h`).
//
// INFO1a/INFO1c carry the *results* of Phase 2 line probing -- the
// actual achievable data rate per symbol rate, pre-emphasis filter
// choice, power reduction, and a measured frequency offset of the
// probing tones. This codebase has none of the machinery that would
// produce real values for any of that (no trellis encoder, no QAM
// constellation mapper, no adaptive equalizer, no actual L1/L2
// channel-response analysis -- see docs/ROADMAP.md Phase 4), so
// `MakeDefaultLocalInfo1c()`/`MakeDefaultLocalInfo1a()` report the
// single symbol rate this project's own `V34Info0` ever claims
// (2400 baud) as having projected data rate 0 -- the spec's own
// "cannot be used" value -- not because any real measurement found
// the line unusable, but because there is truly nothing downstream
// that could use *any* rate yet. This is the honest value, not a
// placeholder standing in for a better one.
struct V34ProbingResult {
    bool highCarrier = false;               // "high carrier frequency to be used" flag
    std::uint8_t preEmphasisFilter = 0;      // 0-10, Tables 3/4's filter index
    std::uint8_t projectedDataRateMultiplier = 0; // 0-14, x2400 bit/s; 0 = "cannot be used"
};

struct V34Info1c {
    std::uint8_t minPowerReductionDb = 0;        // bits 12:14, 0-7
    std::uint8_t additionalPowerReductionDb = 0; // bits 15:17, 0-7
    std::uint8_t mdLengthIn35msUnits = 0;        // bits 18:24, 0-127 (0 = no MD extension)
    V34ProbingResult rate2400;                   // bits 25:33 -- the only rate ever meaningfully populated
    V34ProbingResult rate2743;                   // bits 34:42 -- unsupported by this project; always default
    V34ProbingResult rate2800;                   // bits 43:51
    V34ProbingResult rate3000;                   // bits 52:60
    V34ProbingResult rate3200;                   // bits 61:69
    V34ProbingResult rate3429;                   // bits 70:78
    // bits 79:88, 10-bit two's complement, 0.02Hz units; -512 is the
    // spec's own "not measured accurately enough, ignore this field"
    // sentinel -- always -512 here since no frequency-offset
    // measurement of the probing tones is implemented.
    std::int16_t frequencyOffsetRaw = -512;
};

struct V34Info1a {
    std::uint8_t minPowerReductionDb = 0;        // bits 12:14
    std::uint8_t additionalPowerReductionDb = 0; // bits 15:17
    std::uint8_t mdLengthIn35msUnits = 0;        // bits 18:24
    bool highCarrier = false;                    // bit 25 (call->answer direction)
    std::uint8_t preEmphasisFilter = 0;           // bits 26:29
    std::uint8_t projectedDataRateMultiplier = 0; // bits 30:33 (call->answer direction)
    std::uint8_t symbolRateAnswerToCall = 0;       // bits 34:36, 0-5 (0=2400 .. 5=3429)
    std::uint8_t symbolRateCallToAnswer = 0;       // bits 37:39
    std::int16_t frequencyOffsetRaw = -512;        // bits 40:49, same sentinel as INFO1c's

    V34Info1a() = default;
};

V34Info1c MakeDefaultLocalInfo1c();
V34Info1a MakeDefaultLocalInfo1a();

std::vector<bool> EncodeInfo1c(const V34Info1c& info);
std::optional<V34Info1c> DecodeInfo1c(const std::vector<bool>& bits);

std::vector<bool> EncodeInfo1a(const V34Info1a& info);
std::optional<V34Info1a> DecodeInfo1a(const std::vector<bool>& bits);

} // namespace voipmodem::dsp
