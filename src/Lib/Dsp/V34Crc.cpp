#include "V34Crc.h"

namespace voipmodem::dsp {

std::uint16_t ComputeV34Crc16(const std::vector<bool>& bits) {
    std::uint16_t reg = 0xFFFF;
    for (bool bit : bits) {
        const bool lsb = (reg & 0x0001u) != 0;
        reg >>= 1;
        if (lsb != bit) {
            reg ^= 0x8408u; // x^16 + x^12 + x^5 + 1, reflected
        }
    }
    return reg;
}

} // namespace voipmodem::dsp
