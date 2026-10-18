#include "V8Message.h"

#include <tuple>

namespace voipmodem::dsp {

namespace {

constexpr std::uint8_t Bit(bool value, int position) {
    return value ? static_cast<std::uint8_t>(1u << position) : static_cast<std::uint8_t>(0);
}

} // namespace

std::uint8_t MakeCategoryOctet(V8CategoryTag tag, bool optionBit5, bool optionBit6, bool optionBit7) {
    // b0-b3 = tag (b4 forced to 0 by construction: tag values are all < 16
    // and bit 4 is never part of the tag), b5-b7 = option bits.
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(tag) | Bit(optionBit5, 5) | Bit(optionBit6, 6) |
                                      Bit(optionBit7, 7));
}

std::uint8_t MakeExtensionOctet(bool optionBit0, bool optionBit1, bool optionBit2, bool optionBit6,
                                 bool optionBit7) {
    // b3 = 0, b4 = 1, b5 = 0 (fixed, per 5.2); b0-b2, b6-b7 = option bits.
    return static_cast<std::uint8_t>(Bit(optionBit0, 0) | Bit(optionBit1, 1) | Bit(optionBit2, 2) | Bit(true, 4) |
                                      Bit(optionBit6, 6) | Bit(optionBit7, 7));
}

bool IsCategoryOctet(std::uint8_t octet) {
    return ((octet >> 4) & 0x01) == 0;
}

bool IsExtensionOctet(std::uint8_t octet) {
    return ((octet >> 4) & 0x01) == 1;
}

std::optional<V8CategoryTag> GetCategoryTag(std::uint8_t octet) {
    if (!IsCategoryOctet(octet)) {
        return std::nullopt;
    }
    const std::uint8_t tagValue = octet & 0x0F;
    switch (static_cast<V8CategoryTag>(tagValue)) {
        case V8CategoryTag::CallFunction:
        case V8CategoryTag::ModulationModes:
        case V8CategoryTag::PcmModemAvailability:
        case V8CategoryTag::Protocols:
        case V8CategoryTag::PstnAccess:
        case V8CategoryTag::NonStandardFacilities:
            return static_cast<V8CategoryTag>(tagValue);
        default:
            return std::nullopt; // not a tag this implementation recognizes
    }
}

namespace {

std::optional<std::tuple<bool, bool, bool>> DecodeCategoryOptionBits(std::uint8_t octet, V8CategoryTag expectedTag) {
    const auto tag = GetCategoryTag(octet);
    if (!tag.has_value() || tag.value() != expectedTag) {
        return std::nullopt;
    }
    return std::make_tuple(((octet >> 5) & 0x01) != 0, ((octet >> 6) & 0x01) != 0, ((octet >> 7) & 0x01) != 0);
}

} // namespace

std::uint8_t EncodeCallFunctionOctet(V8CallFunction function) {
    bool b5 = false, b6 = false, b7 = false;
    switch (function) {
        case V8CallFunction::ToBeDetermined: b5 = false; b6 = false; b7 = false; break;
        case V8CallFunction::PstnMultimediaTerminal: b5 = true; b6 = false; b7 = false; break;
        case V8CallFunction::Textphone: b5 = false; b6 = true; b7 = false; break;
        case V8CallFunction::Videotext: b5 = true; b6 = true; b7 = false; break;
        case V8CallFunction::TransmitFacsimile: b5 = false; b6 = false; b7 = true; break;
        case V8CallFunction::ReceiveFacsimile: b5 = true; b6 = false; b7 = true; break;
        case V8CallFunction::Data: b5 = false; b6 = true; b7 = true; break;
        case V8CallFunction::IndicatedInExtensionOctet: b5 = true; b6 = true; b7 = true; break;
    }
    return MakeCategoryOctet(V8CategoryTag::CallFunction, b5, b6, b7);
}

std::optional<V8CallFunction> DecodeCallFunctionOctet(std::uint8_t octet) {
    const auto bits = DecodeCategoryOptionBits(octet, V8CategoryTag::CallFunction);
    if (!bits.has_value()) {
        return std::nullopt;
    }
    const auto [b5, b6, b7] = bits.value();
    if (!b5 && !b6 && !b7) return V8CallFunction::ToBeDetermined;
    if (b5 && !b6 && !b7) return V8CallFunction::PstnMultimediaTerminal;
    if (!b5 && b6 && !b7) return V8CallFunction::Textphone;
    if (b5 && b6 && !b7) return V8CallFunction::Videotext;
    if (!b5 && !b6 && b7) return V8CallFunction::TransmitFacsimile;
    if (b5 && !b6 && b7) return V8CallFunction::ReceiveFacsimile;
    if (!b5 && b6 && b7) return V8CallFunction::Data;
    return V8CallFunction::IndicatedInExtensionOctet; // b5 && b6 && b7
}

std::vector<std::uint8_t> EncodeModulationModesOctets(const V8ModulationModes& modes) {
    std::vector<std::uint8_t> octets;
    octets.push_back(
        MakeCategoryOctet(V8CategoryTag::ModulationModes, modes.pcmModemAvailabilityPresent, modes.v34Duplex,
                           modes.v34HalfDuplex));

    const bool needsModn1 = modes.v32bisOrV32 || modes.v22bisOrV22 || modes.v17;
    const bool needsModn2 = modes.v26ter || modes.v26bis || modes.v23Duplex || modes.v23HalfDuplex || modes.v21;
    const bool needsModn2PositionOnly = modes.v29HalfDuplex || modes.v27ter;

    if (needsModn1 || needsModn2 || needsModn2PositionOnly) {
        octets.push_back(
            MakeExtensionOctet(modes.v32bisOrV32, modes.v22bisOrV22, modes.v17, modes.v29HalfDuplex, modes.v27ter));
    }
    if (needsModn2) {
        octets.push_back(
            MakeExtensionOctet(modes.v26ter, modes.v26bis, modes.v23Duplex, modes.v23HalfDuplex, modes.v21));
    }
    return octets;
}

std::optional<V8ModulationModes> DecodeModulationModesOctets(const std::vector<std::uint8_t>& octets,
                                                                size_t& index) {
    if (index >= octets.size()) {
        return std::nullopt;
    }
    const auto modn0Bits = DecodeCategoryOptionBits(octets[index], V8CategoryTag::ModulationModes);
    if (!modn0Bits.has_value()) {
        return std::nullopt;
    }

    V8ModulationModes modes;
    const auto [pcmPresent, v34Duplex, v34HalfDuplex] = modn0Bits.value();
    modes.pcmModemAvailabilityPresent = pcmPresent;
    modes.v34Duplex = v34Duplex;
    modes.v34HalfDuplex = v34HalfDuplex;
    size_t cursor = index + 1;

    if (cursor < octets.size() && IsExtensionOctet(octets[cursor])) {
        const std::uint8_t modn1 = octets[cursor];
        modes.v32bisOrV32 = (modn1 & 0x01) != 0;
        modes.v22bisOrV22 = ((modn1 >> 1) & 0x01) != 0;
        modes.v17 = ((modn1 >> 2) & 0x01) != 0;
        modes.v29HalfDuplex = ((modn1 >> 6) & 0x01) != 0;
        modes.v27ter = ((modn1 >> 7) & 0x01) != 0;
        ++cursor;

        if (cursor < octets.size() && IsExtensionOctet(octets[cursor])) {
            const std::uint8_t modn2 = octets[cursor];
            modes.v26ter = (modn2 & 0x01) != 0;
            modes.v26bis = ((modn2 >> 1) & 0x01) != 0;
            modes.v23Duplex = ((modn2 >> 2) & 0x01) != 0;
            modes.v23HalfDuplex = ((modn2 >> 6) & 0x01) != 0;
            modes.v21 = ((modn2 >> 7) & 0x01) != 0;
            ++cursor;
        }
    }

    index = cursor;
    return modes;
}

std::uint8_t EncodePcmModemAvailabilityOctet(const V8PcmModemAvailability& pcm) {
    return MakeCategoryOctet(V8CategoryTag::PcmModemAvailability, pcm.v90OrV92Analogue, pcm.v90OrV92Digital,
                              pcm.v91);
}

std::optional<V8PcmModemAvailability> DecodePcmModemAvailabilityOctet(std::uint8_t octet) {
    const auto bits = DecodeCategoryOptionBits(octet, V8CategoryTag::PcmModemAvailability);
    if (!bits.has_value()) {
        return std::nullopt;
    }
    const auto [b5, b6, b7] = bits.value();
    V8PcmModemAvailability pcm;
    pcm.v90OrV92Analogue = b5;
    pcm.v90OrV92Digital = b6;
    pcm.v91 = b7;
    return pcm;
}

std::uint8_t EncodeProtocolOctet(V8Protocol protocol) {
    switch (protocol) {
        case V8Protocol::Lapm: return MakeCategoryOctet(V8CategoryTag::Protocols, true, false, false);
        case V8Protocol::IndicatedInExtensionOctet: return MakeCategoryOctet(V8CategoryTag::Protocols, true, true, true);
    }
    return MakeCategoryOctet(V8CategoryTag::Protocols, true, false, false);
}

std::optional<V8Protocol> DecodeProtocolOctet(std::uint8_t octet) {
    const auto bits = DecodeCategoryOptionBits(octet, V8CategoryTag::Protocols);
    if (!bits.has_value()) {
        return std::nullopt;
    }
    const auto [b5, b6, b7] = bits.value();
    if (b5 && !b6 && !b7) return V8Protocol::Lapm;
    if (b5 && b6 && b7) return V8Protocol::IndicatedInExtensionOctet;
    return std::nullopt; // not one of the two patterns Table 6 actually defines
}

std::uint8_t EncodePstnAccessOctet(const V8PstnAccess& access) {
    return MakeCategoryOctet(V8CategoryTag::PstnAccess, access.callDceCellular, access.answerDceCellular,
                              access.digitalNetwork);
}

std::optional<V8PstnAccess> DecodePstnAccessOctet(std::uint8_t octet) {
    const auto bits = DecodeCategoryOptionBits(octet, V8CategoryTag::PstnAccess);
    if (!bits.has_value()) {
        return std::nullopt;
    }
    const auto [b5, b6, b7] = bits.value();
    V8PstnAccess access;
    access.callDceCellular = b5;
    access.answerDceCellular = b6;
    access.digitalNetwork = b7;
    return access;
}

std::vector<std::uint8_t> BuildCjOctets() {
    return {0x00, 0x00, 0x00};
}

} // namespace voipmodem::dsp
