#pragma once

#include <optional>
#include <vector>

#include "DspTypes.h"
#include "FskDemodulator.h"
#include "FskModulator.h"
#include "V8CallMenu.h"
#include "V8Message.h"

namespace voipmodem::dsp {

// Builds one instance of a CI, CM/JM, or CJ bit sequence (V.8 clause 5:
// preamble + octets, each octet 1-start+8-data(LSB-first)+1-stop) as
// PCM samples, via an FskModulator already configured for the correct
// V.21 channel (CI/CM on channel 1, JM on channel 2 -- caller's
// responsibility, same as CallAudioBridge already threads
// `callingParty` through for the FSK counter-byte probe).
//
// These only build a single sequence's worth of samples -- the
// repetition cadence and ON/OFF timing (clause 7.1 for CI, clause 8 for
// the rest) is V8Handshake's job, not this module's.
std::vector<Sample> BuildCiSequence(FskModulator& modulator, V8CallFunction callFunction);
std::vector<Sample> BuildCmOrJmSequence(FskModulator& modulator, const V8CallMenu& menu);
// CJ has NO preamble (3.5) -- just three octets of all ZEROs, each with
// its own start/stop bits.
std::vector<Sample> BuildCjSequence(FskModulator& modulator);

// Bit-stream scanning: finds `pattern` as a contiguous subsequence of
// `bits`, searching from `fromIndex` onward. Used to locate a CI or
// CM/JM preamble (10 ONEs + the signal-specific 10-bit sync pattern,
// Table 1/V.8) in a continuously-demodulated bitstream.
//
// Caller must advance `fromIndex` monotonically across calls (e.g. to
// just past the last preamble found, or the end of what's already been
// scanned) rather than always searching from 0 -- re-scanning a
// forever-growing buffer from the start every call is exactly the
// unbounded-growth mistake FskDemodulator's first version made (see
// docs/ROADMAP.md Phase 4); this function itself has no memory of past
// calls to protect against that.
std::optional<size_t> FindBitPattern(const std::vector<bool>& bits, const std::vector<bool>& pattern,
                                      size_t fromIndex);

// After a preamble has been located (`index` = the bit position just
// past it, e.g. FindBitPattern's result + pattern.size()), greedily
// decodes consecutive 1-start/8-data/1-stop octets -- reusing
// FskDemodulator::TryDecodeAsyncByte's exact framing -- until framing
// fails. That failure is expected and is how one sequence naturally
// ends: the next repetition's preamble starts with 10 mark/ONE bits,
// which can never be a valid start bit (always space/0), so decoding
// stops right there on its own.
//
// Groups the decoded octets into a V8CallMenu via DecodeCallMenu.
// Advances `index` past every octet consumed (including any trailing
// octets DecodeCallMenu itself didn't interpret, e.g. non-standard
// facilities -- see its own header comment). Returns nullopt without
// advancing `index` either if the first octet isn't a valid
// call-function octet at all, OR if `bits` simply doesn't contain
// enough buffered data yet to reach a genuine framing failure (the
// real end-of-sequence marker) -- these are deliberately NOT
// distinguished from each other, so a caller must always be prepared
// to retry from the same `index` once more bits arrive, not just on a
// provably-false-positive preamble match.
std::optional<V8CallMenu> ReadCallMenuAfterPreamble(const std::vector<bool>& bits, size_t& index);

// Same idea, for CJ: no preamble to skip, just reads up to 3 octets
// from `bits[index..]` via the same async framing and checks whether
// all 3 decoded as all-ZERO (3.5). Does NOT advance `index` -- CJ
// detection is a non-destructive "has this arrived yet" check, since
// (unlike CI/CM/JM) there's no preamble to resynchronize on if this
// guess at alignment turns out wrong.
bool LooksLikeCj(const std::vector<bool>& bits, size_t index);

} // namespace voipmodem::dsp
