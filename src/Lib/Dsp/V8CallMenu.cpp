#include "V8CallMenu.h"

namespace voipmodem::dsp {

std::vector<std::uint8_t> EncodeCallMenu(const V8CallMenu& menu) {
    std::vector<std::uint8_t> octets;
    octets.push_back(EncodeCallFunctionOctet(menu.callFunction));

    if (menu.modulationModes.has_value()) {
        const std::vector<std::uint8_t> modOctets = EncodeModulationModesOctets(menu.modulationModes.value());
        octets.insert(octets.end(), modOctets.begin(), modOctets.end());
    }
    if (menu.protocol.has_value()) {
        octets.push_back(EncodeProtocolOctet(menu.protocol.value()));
    }
    if (menu.pstnAccess.has_value()) {
        octets.push_back(EncodePstnAccessOctet(menu.pstnAccess.value()));
    }
    if (menu.pcmModemAvailability.has_value()) {
        octets.push_back(EncodePcmModemAvailabilityOctet(menu.pcmModemAvailability.value()));
    }
    return octets;
}

std::optional<V8CallMenu> DecodeCallMenu(const std::vector<std::uint8_t>& octets, size_t& index) {
    if (index >= octets.size()) {
        return std::nullopt;
    }

    const auto callFunction = DecodeCallFunctionOctet(octets[index]);
    if (!callFunction.has_value()) {
        return std::nullopt; // call function is mandatory and must come first
    }

    V8CallMenu menu;
    menu.callFunction = callFunction.value();
    size_t cursor = index + 1;

    while (cursor < octets.size()) {
        const auto tag = GetCategoryTag(octets[cursor]);
        if (!tag.has_value()) {
            break; // not a category octet this implementation recognizes -- stop, don't guess
        }

        switch (tag.value()) {
            case V8CategoryTag::ModulationModes: {
                size_t modIndex = cursor;
                const auto modes = DecodeModulationModesOctets(octets, modIndex);
                if (!modes.has_value()) {
                    return menu; // tag matched but decode failed somehow -- stop here, keep what we have
                }
                menu.modulationModes = modes;
                cursor = modIndex;
                break;
            }
            case V8CategoryTag::Protocols: {
                const auto protocol = DecodeProtocolOctet(octets[cursor]);
                if (!protocol.has_value()) {
                    return menu;
                }
                menu.protocol = protocol;
                ++cursor;
                break;
            }
            case V8CategoryTag::PstnAccess: {
                const auto access = DecodePstnAccessOctet(octets[cursor]);
                if (!access.has_value()) {
                    return menu;
                }
                menu.pstnAccess = access;
                ++cursor;
                break;
            }
            case V8CategoryTag::PcmModemAvailability: {
                const auto pcm = DecodePcmModemAvailabilityOctet(octets[cursor]);
                if (!pcm.has_value()) {
                    return menu;
                }
                menu.pcmModemAvailability = pcm;
                ++cursor;
                break;
            }
            case V8CategoryTag::CallFunction:
            case V8CategoryTag::NonStandardFacilities:
            default:
                // A second call-function octet shouldn't appear; non-standard
                // facilities' variable-length sub-format isn't modeled. Either
                // way, stop rather than misinterpret what follows.
                index = cursor;
                return menu;
        }
    }

    index = cursor;
    return menu;
}

V8ModulationModes IntersectModulationModes(const V8ModulationModes& a, const V8ModulationModes& b) {
    V8ModulationModes result;
    result.pcmModemAvailabilityPresent = false;
    result.v34Duplex = a.v34Duplex && b.v34Duplex;
    result.v34HalfDuplex = a.v34HalfDuplex && b.v34HalfDuplex;
    result.v32bisOrV32 = a.v32bisOrV32 && b.v32bisOrV32;
    result.v22bisOrV22 = a.v22bisOrV22 && b.v22bisOrV22;
    result.v17 = a.v17 && b.v17;
    result.v29HalfDuplex = a.v29HalfDuplex && b.v29HalfDuplex;
    result.v27ter = a.v27ter && b.v27ter;
    result.v26ter = a.v26ter && b.v26ter;
    result.v26bis = a.v26bis && b.v26bis;
    result.v23Duplex = a.v23Duplex && b.v23Duplex;
    result.v23HalfDuplex = a.v23HalfDuplex && b.v23HalfDuplex;
    result.v21 = a.v21 && b.v21;
    return result;
}

std::optional<V8ModulationModeItem> SelectModulationMode(const V8ModulationModes& modes) {
    if (modes.v34Duplex) return V8ModulationModeItem::V34Duplex;
    if (modes.v34HalfDuplex) return V8ModulationModeItem::V34HalfDuplex;
    if (modes.v32bisOrV32) return V8ModulationModeItem::V32bisOrV32;
    if (modes.v22bisOrV22) return V8ModulationModeItem::V22bisOrV22;
    if (modes.v17) return V8ModulationModeItem::V17;
    if (modes.v29HalfDuplex) return V8ModulationModeItem::V29HalfDuplex;
    if (modes.v27ter) return V8ModulationModeItem::V27ter;
    if (modes.v26ter) return V8ModulationModeItem::V26ter;
    if (modes.v26bis) return V8ModulationModeItem::V26bis;
    if (modes.v23Duplex) return V8ModulationModeItem::V23Duplex;
    if (modes.v23HalfDuplex) return V8ModulationModeItem::V23HalfDuplex;
    if (modes.v21) return V8ModulationModeItem::V21;
    return std::nullopt;
}

} // namespace voipmodem::dsp
