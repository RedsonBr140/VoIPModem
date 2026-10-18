#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "V8Message.h"

namespace voipmodem::dsp {

// Bundles the octet categories that make up one CM or JM sequence's
// payload (V.8 clause 7.3/7.4) into a single value, on top of the
// per-category codec in V8Message.h. Call function is mandatory (5:
// "the first information category in a sequence shall be the call
// function"); everything else is optional, matching real CM/JM
// content which varies by what each DCE actually supports.
struct V8CallMenu {
    V8CallFunction callFunction = V8CallFunction::Data;
    std::optional<V8ModulationModes> modulationModes;
    std::optional<V8Protocol> protocol;
    std::optional<V8PstnAccess> pstnAccess;
    std::optional<V8PcmModemAvailability> pcmModemAvailability;
};

// Call function octet first, then whichever optional categories are
// present (order among the optional ones is this implementation's own
// choice -- the spec says "No particular ordering is required for
// subsequent information categories").
std::vector<std::uint8_t> EncodeCallMenu(const V8CallMenu& menu);

// Walks octets[index..], grouping octets into categories one at a
// time, until an octet doesn't belong to a category this
// implementation recognizes (includes running off the end of
// `octets`). Advances `index` past everything consumed. Returns
// nullopt without advancing `index` if the very first octet isn't a
// call-function category octet -- call function is mandatory and must
// come first, so anything else there means this isn't a CM/JM payload
// at all.
//
// Known, stated limitation: non-standard facilities (6.6) are a
// recognized category tag but this implementation does not know their
// variable-length sub-format, so hitting one simply stops the scan
// (whatever was decoded before it is still returned) rather than
// skipping over it correctly.
std::optional<V8CallMenu> DecodeCallMenu(const std::vector<std::uint8_t>& octets, size_t& index);

// Field-by-field AND of two modulation-modes sets -- what JM's modes
// should be when PCM isn't involved (7.4: "JM shall include the octets
// necessary to indicate all modulation modes that are both indicated
// in CM and available in the answer DCE"). Always clears the PCM-
// availability-present flag: this codebase doesn't implement V.90/91/92
// PCM modems, so it must never claim that category is coming.
V8ModulationModes IntersectModulationModes(const V8ModulationModes& a, const V8ModulationModes& b);

// Table 4's own item numbers (1-12; item 0 is the PCM-presence flag,
// not a mode, and is deliberately excluded here). 7.4's last paragraph:
// when PCM modem availability isn't in JM, "the indicated modulation
// category modulation mode with the lowest item number... shall be used
// in the subsequent data session" -- i.e. lower numbers are preferred.
enum class V8ModulationModeItem {
    V34Duplex = 1,
    V34HalfDuplex = 2,
    V32bisOrV32 = 3,
    V22bisOrV22 = 4,
    V17 = 5,
    V29HalfDuplex = 6,
    V27ter = 7,
    V26ter = 8,
    V26bis = 9,
    V23Duplex = 10,
    V23HalfDuplex = 11,
    V21 = 12,
};

// Returns the lowest-item-number mode set in `modes`, or nullopt if
// none are -- which per 7.4 means negotiation found no common mode at
// all (a real failure case this implementation doesn't otherwise
// enumerate as its own error).
std::optional<V8ModulationModeItem> SelectModulationMode(const V8ModulationModes& modes);

} // namespace voipmodem::dsp
