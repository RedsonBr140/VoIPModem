#include "V8Signal.h"

namespace voipmodem::dsp {

std::vector<Sample> BuildCiSequence(FskModulator& modulator, V8CallFunction callFunction) {
    std::vector<Sample> samples;
    for (bool bit : kV8PreambleOnes) {
        modulator.ModulateBit(samples, bit);
    }
    for (bool bit : kV8CiSyncBits) {
        modulator.ModulateBit(samples, bit);
    }
    modulator.ModulateByteAsync(samples, EncodeCallFunctionOctet(callFunction));
    return samples;
}

std::vector<Sample> BuildCmOrJmSequence(FskModulator& modulator, const V8CallMenu& menu) {
    std::vector<Sample> samples;
    for (bool bit : kV8PreambleOnes) {
        modulator.ModulateBit(samples, bit);
    }
    for (bool bit : kV8CmJmSyncBits) {
        modulator.ModulateBit(samples, bit);
    }
    for (std::uint8_t octet : EncodeCallMenu(menu)) {
        modulator.ModulateByteAsync(samples, octet);
    }
    return samples;
}

std::vector<Sample> BuildCjSequence(FskModulator& modulator) {
    std::vector<Sample> samples;
    for (std::uint8_t octet : BuildCjOctets()) {
        modulator.ModulateByteAsync(samples, octet);
    }
    return samples;
}

std::optional<size_t> FindBitPattern(const std::vector<bool>& bits, const std::vector<bool>& pattern,
                                      size_t fromIndex) {
    if (pattern.empty() || bits.size() < pattern.size()) {
        return std::nullopt;
    }
    const size_t lastStart = bits.size() - pattern.size();
    for (size_t start = fromIndex; start <= lastStart; ++start) {
        bool matches = true;
        for (size_t i = 0; i < pattern.size(); ++i) {
            if (bits[start + i] != pattern[i]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return start;
        }
    }
    return std::nullopt;
}

std::optional<V8CallMenu> ReadCallMenuAfterPreamble(const std::vector<bool>& bits, size_t& index) {
    std::vector<std::uint8_t> octets;
    size_t cursor = index;

    // Only decode once a GENUINE framing failure is hit (start/stop bits
    // wrong -- the real end-of-sequence marker, since the next
    // repetition's preamble starts with mark/1 bits that can never be a
    // valid start bit). Running out of *buffered* bits before that point
    // is a different, inconclusive case -- it means the rest of this
    // sequence just hasn't arrived yet, not that framing failed -- and
    // must NOT be treated the same way, or a multi-octet menu gets read
    // as a truncated, incomplete one the moment it's checked a bit too
    // early. (Caught via this exact symptom in V8Handshake's own
    // loopback test -- see docs/ROADMAP.md Phase 4.)
    while (cursor + 10 <= bits.size()) {
        const auto decoded = FskDemodulator::TryDecodeAsyncByte(bits, cursor);
        if (!decoded.has_value()) {
            size_t octetIndex = 0;
            const auto menu = DecodeCallMenu(octets, octetIndex);
            if (!menu.has_value()) {
                return std::nullopt; // first octet wasn't callFunction at all -- index left unchanged
            }
            index = cursor;
            return menu;
        }
        octets.push_back(decoded.value());
    }

    return std::nullopt; // ran out of buffered bits -- inconclusive, index left unchanged
}

bool LooksLikeCj(const std::vector<bool>& bits, size_t index) {
    size_t cursor = index;
    const std::vector<std::uint8_t> expected = BuildCjOctets(); // {0, 0, 0}
    for (std::uint8_t expectedOctet : expected) {
        const auto decoded = FskDemodulator::TryDecodeAsyncByte(bits, cursor);
        if (!decoded.has_value() || decoded.value() != expectedOctet) {
            return false;
        }
    }
    return true;
}

} // namespace voipmodem::dsp
