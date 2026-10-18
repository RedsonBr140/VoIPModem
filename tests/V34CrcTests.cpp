#include "../src/Lib/Dsp/V34Crc.h"
#include "TestFramework.h"

using namespace voipmodem::dsp;

namespace {

std::vector<bool> AsciiBitsLsbFirst(const char* text) {
    std::vector<bool> bits;
    for (const char* p = text; *p != '\0'; ++p) {
        const unsigned char byte = static_cast<unsigned char>(*p);
        for (int i = 0; i < 8; ++i) {
            bits.push_back(((byte >> i) & 0x01) != 0);
        }
    }
    return bits;
}

} // namespace

// Independent correctness check, not tied to any V.34-specific worked
// example (the Recommendation text doesn't give one): the well-known
// "CRC-16/X-25" check value for ASCII "123456789" is 0x906E (poly
// 0x1021 reflected, preset 0xFFFF, reflected in/out, final XOR 0xFFFF).
// V34Crc.h's own comment explains why ComputeV34Crc16 omits that final
// XOR (10.1.2.3.2 only says to output the raw register contents), so
// the value this test expects is 0x906E with that inversion undone --
// confirming the core shift-register math (poly, preset, reflection)
// matches that standard algorithm exactly, which is the part
// independently verifiable without a real V.34 modem to test against.
TEST(V34Crc_MatchesCrc16X25CoreMathOnStandardTestVector) {
    const std::vector<bool> bits = AsciiBitsLsbFirst("123456789");
    const std::uint16_t result = ComputeV34Crc16(bits);
    constexpr std::uint16_t kCrc16X25Check = 0x906E;
    EXPECT_EQ(static_cast<int>(result), static_cast<int>(kCrc16X25Check ^ 0xFFFFu));
}

TEST(V34Crc_EmptyInput_ReturnsThePresetValueUnchanged) {
    // Nothing shifted in -- the register should still read its initial
    // preset, 0xFFFF, per 10.1.2.3.2 steps 1-2.
    EXPECT_EQ(static_cast<int>(ComputeV34Crc16({})), static_cast<int>(0xFFFFu));
}

TEST(V34Crc_FlippingAnyInputBit_ChangesTheResult) {
    const std::vector<bool> original = AsciiBitsLsbFirst("123456789");
    const std::uint16_t originalCrc = ComputeV34Crc16(original);

    for (size_t i = 0; i < original.size(); ++i) {
        std::vector<bool> flipped = original;
        flipped[i] = !flipped[i];
        EXPECT_TRUE(ComputeV34Crc16(flipped) != originalCrc);
    }
}
