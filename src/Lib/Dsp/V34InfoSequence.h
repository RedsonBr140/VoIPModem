#pragma once

#include <optional>
#include <vector>

#include "DspTypes.h"
#include "V34Info0.h"

namespace voipmodem::dsp {

// Glues V34Info0's bit-level codec to DbpskModulator/DbpskDemodulator so a
// real INFO0 sequence can be turned into/out of actual audio samples --
// the seam V34Info0.h's own class comment flagged as not yet done.
//
// Still deliberately scoped, same discipline as every other increment this
// phase: this is only "INFO0 bits <-> DBPSK audio at a caller-supplied
// carrier," nothing more. Per 10.1.2.3.1, confirmed against the real
// Recommendation text, the *carrier itself* differs by role -- 2400Hz for
// the answering modem's INFO0a, 1200Hz for the calling modem's INFO0c --
// and each carries its own separately-mixed guard tone. Choosing which
// carrier applies, mixing in the guard tone, and anything about Phase 2
// sequencing/timing/retries (11.2) all stay a CallAudioBridge/Phase-2
// concern, not this module's -- the same separation ToneGenerator/AnsamTone
// already keep from FskModulator. NOT yet wired into CallAudioBridge or
// V8Handshake.
constexpr double kAnswerModemInfoCarrierHz = 2400.0;
constexpr double kCallModemInfoCarrierHz = 1200.0;
constexpr double kInfoBaudRate = 600.0;

// Appends one full INFO0 sequence (DbpskModulator's arbitrary-phase
// reference symbol, then the 49 data bits from EncodeInfo0) to `out` at
// `carrierHz`/kInfoBaudRate.
void EncodeInfo0ToSamples(const V34Info0& info, double carrierHz, std::vector<Sample>& out,
                           int sampleRateHz = kSampleRateHz);

// Single-shot decode of one full INFO0 sequence from `samples` (the
// reference symbol plus 49 data symbols -- 50 symbols worth of audio, no
// more, no less expected) at `carrierHz`/kInfoBaudRate. Unlike
// DbpskDemodulator::ProcessSamples itself, this is not a streaming/chunked
// API: INFO0 is always exactly 49 bits, so there is no reason to decode it
// incrementally. Returns nullopt on a short/garbled buffer, or on a
// frame-sync/CRC mismatch from DecodeInfo0 -- both cases are just "this
// wasn't a correctly-received INFO0 sequence," not distinguished further.
std::optional<V34Info0> DecodeInfo0FromSamples(const std::vector<Sample>& samples, double carrierHz,
                                                int sampleRateHz = kSampleRateHz);

} // namespace voipmodem::dsp
