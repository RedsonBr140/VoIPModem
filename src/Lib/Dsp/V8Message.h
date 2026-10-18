#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace voipmodem::dsp {

// Bit-level codec for the category/extension octets that make up
// ITU-T V.8's CI, CM and JM signals -- confirmed against the actual
// ITU-T Recommendation V.8 (11/2000) text, clauses 5 (coding format)
// and 6 (code tables), not guessed from secondary sources (see
// docs/ROADMAP.md Phase 4 for how that text was obtained and read).
//
// Each octet here is exactly the uint8_t that FskModulator::
// ModulateByteAsync / FskDemodulator::TryDecodeAsyncByte already carry
// as a 1-start + 8-data(LSB-first) + 1-stop async frame -- V.8 5.1
// says "Bits b0-b3 make up the category tag with b0 the least
// significant bit", the same LSB-first convention. This module only
// interprets what the 8 bits mean; it does not touch audio or framing
// at all.
//
// What this increment does NOT yet do (future increments, see
// docs/ROADMAP.md Phase 4): assemble/parse a full multi-category CM or
// JM octet sequence, build the CI/CM/JM/CJ bit sequences (preamble +
// octets) and feed them through Fsk*, generate/detect ANSam, or
// implement the clause 8 state machine (timers, retries, ANSam-then-CM
// ordering). This is deliberately scoped to just the octet coding
// format in isolation, the same discipline used for Dds/ToneGenerator/
// GoertzelDetector before FskModulator/FskDemodulator existed, and for
// those before CallAudioBridge wired them into real call audio.

// Table 1/V.8 -- the 20-bit preamble (10 ONEs + 10 sync bits) that
// precedes every CI/CM/JM octet sequence. Not octet-framed: these are
// raw bits, meant for FskModulator::ModulateBit, not ModulateByteAsync.
inline constexpr bool kV8PreambleOnes[10] = {true, true, true, true, true, true, true, true, true, true};
inline constexpr bool kV8CiSyncBits[10] = {false, false, false, false, false, false, false, false, false, true};
inline constexpr bool kV8CmJmSyncBits[10] = {false, false, false, false, false, false, true, true, true, true};

// 5.1/Table 2 -- category tags (b0-b3, b0 = LSB). Values confirmed by
// decoding each row of Table 2 bit-by-bit, not copied from a summary.
enum class V8CategoryTag : std::uint8_t {
    CallFunction = 1,
    ModulationModes = 5,
    PcmModemAvailability = 7,
    Protocols = 10,
    PstnAccess = 13,
    NonStandardFacilities = 15,
};

// 5.1: start-bit(0) b0 b1 b2 b3 0 b5 b6 b7 stop-bit(1). Builds the
// 8-bit value (the start/stop bits themselves are FskModulator's job).
std::uint8_t MakeCategoryOctet(V8CategoryTag tag, bool optionBit5, bool optionBit6, bool optionBit7);

// 5.2: start-bit(0) b0 b1 b2 0 1 0 b6 b7 stop-bit(1).
std::uint8_t MakeExtensionOctet(bool optionBit0, bool optionBit1, bool optionBit2, bool optionBit6,
                                 bool optionBit7);

// b4 == 0 identifies a category octet (5.1); b4 == 1 identifies an
// extension octet of whichever category octet preceded it (5.2).
bool IsCategoryOctet(std::uint8_t octet);
bool IsExtensionOctet(std::uint8_t octet);

// Valid only when IsCategoryOctet(octet) is true.
std::optional<V8CategoryTag> GetCategoryTag(std::uint8_t octet);

// Table 3/V.8 -- the call function category (one octet, "callf0").
// Option bits b5b6b7 confirmed row-by-row against the spec text.
enum class V8CallFunction : std::uint8_t {
    ToBeDetermined,         // 000 -- textphone per ITU-T V.18, "to be determined"
    PstnMultimediaTerminal, // 100
    Textphone,              // 010
    Videotext,               // 110
    TransmitFacsimile,       // 001
    ReceiveFacsimile,        // 101
    Data,                    // 011 -- the one this project actually needs
    IndicatedInExtensionOctet, // 111
};

std::uint8_t EncodeCallFunctionOctet(V8CallFunction function);
// Returns nullopt if `octet` isn't a call-function category octet at all
// (wrong tag) -- distinct from decoding a recognized-but-unused value,
// which cannot happen since all 8 combinations of 3 bits are assigned.
std::optional<V8CallFunction> DecodeCallFunctionOctet(std::uint8_t octet);

// Table 4/V.8 -- modulation modes, up to 3 octets (modn0/modn1/modn2:
// one category octet plus up to two extension octets). Field-to-bit
// mapping confirmed against the raw spec text row by row.
struct V8ModulationModes {
    bool pcmModemAvailabilityPresent = false; // modn0 b5
    bool v34Duplex = false;                   // modn0 b6
    bool v34HalfDuplex = false;                // modn0 b7
    bool v32bisOrV32 = false;                  // modn1 b0
    bool v22bisOrV22 = false;                  // modn1 b1
    bool v17 = false;                          // modn1 b2
    bool v29HalfDuplex = false;                // modn1 b6
    bool v27ter = false;                       // modn1 b7
    bool v26ter = false;                       // modn2 b0
    bool v26bis = false;                       // modn2 b1
    bool v23Duplex = false;                    // modn2 b2
    bool v23HalfDuplex = false;                 // modn2 b6
    bool v21 = false;                           // modn2 b7
};

// Emits modn0 always, and modn1/modn2 only if emitting them would carry
// at least one set bit -- the spec allows the sequence to be as short
// as 1 octet when nothing in modn1/modn2's range applies.
std::vector<std::uint8_t> EncodeModulationModesOctets(const V8ModulationModes& modes);

// Decodes starting at octets[index] (which must be a ModulationModes
// category octet); advances index past every octet consumed (1-3).
// Returns nullopt without advancing index if octets[index] isn't a
// ModulationModes category octet.
std::optional<V8ModulationModes> DecodeModulationModesOctets(const std::vector<std::uint8_t>& octets,
                                                                size_t& index);

// Table 5/V.8 -- PCM modem availability (one octet, "pcm0").
struct V8PcmModemAvailability {
    bool v90OrV92Analogue = false; // pcm0 b5
    bool v90OrV92Digital = false;  // pcm0 b6
    bool v91 = false;              // pcm0 b7
};
std::uint8_t EncodePcmModemAvailabilityOctet(const V8PcmModemAvailability& pcm);
std::optional<V8PcmModemAvailability> DecodePcmModemAvailabilityOctet(std::uint8_t octet);

// Table 6/V.8 -- protocol category (one octet, "prot0"). Only two
// option-bit patterns are defined; anything else is not a valid prot0
// value (not merely an unused-but-decodable one).
enum class V8Protocol : std::uint8_t {
    Lapm,                    // 100 -- calls for LAPM per ITU-T V.42
    IndicatedInExtensionOctet, // 111
};
std::uint8_t EncodeProtocolOctet(V8Protocol protocol);
std::optional<V8Protocol> DecodeProtocolOctet(std::uint8_t octet);

// Table 7/V.8 -- PSTN access category (one octet, "access0").
struct V8PstnAccess {
    bool callDceCellular = false;   // access0 b5
    bool answerDceCellular = false; // access0 b6
    bool digitalNetwork = false;    // access0 b7 (0 = analogue network)
};
std::uint8_t EncodePstnAccessOctet(const V8PstnAccess& access);
std::optional<V8PstnAccess> DecodePstnAccessOctet(std::uint8_t octet);

// 3.5/8.1.2 -- CJ: three consecutive octets of all ZEROs (with their
// own start/stop bits via FskModulator, not included here).
std::vector<std::uint8_t> BuildCjOctets();

} // namespace voipmodem::dsp
