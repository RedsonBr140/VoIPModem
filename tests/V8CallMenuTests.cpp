#include "../src/Lib/Dsp/V8CallMenu.h"
#include "TestFramework.h"

using namespace voipmodem::dsp;

TEST(V8CallMenu_EncodeDecode_CallFunctionOnlyRoundTrips) {
    V8CallMenu menu;
    menu.callFunction = V8CallFunction::Data;

    const std::vector<std::uint8_t> octets = EncodeCallMenu(menu);
    EXPECT_EQ(octets.size(), static_cast<size_t>(1));

    size_t index = 0;
    const auto decoded = DecodeCallMenu(octets, index);
    EXPECT_TRUE(decoded.has_value());
    EXPECT_EQ(static_cast<int>(decoded.value().callFunction), static_cast<int>(V8CallFunction::Data));
    EXPECT_TRUE(!decoded.value().modulationModes.has_value());
    EXPECT_EQ(index, octets.size());
}

TEST(V8CallMenu_EncodeDecode_FullMenuRoundTrips) {
    V8CallMenu menu;
    menu.callFunction = V8CallFunction::Data;
    V8ModulationModes modes;
    modes.v34Duplex = true;
    modes.v32bisOrV32 = true;
    menu.modulationModes = modes;
    menu.protocol = V8Protocol::Lapm;
    V8PstnAccess access;
    access.digitalNetwork = true;
    menu.pstnAccess = access;

    const std::vector<std::uint8_t> octets = EncodeCallMenu(menu);
    size_t index = 0;
    const auto decoded = DecodeCallMenu(octets, index);
    EXPECT_TRUE(decoded.has_value());
    EXPECT_EQ(index, octets.size());

    const V8CallMenu& d = decoded.value();
    EXPECT_EQ(static_cast<int>(d.callFunction), static_cast<int>(V8CallFunction::Data));
    EXPECT_TRUE(d.modulationModes.has_value());
    EXPECT_TRUE(d.modulationModes.value().v34Duplex);
    EXPECT_TRUE(d.modulationModes.value().v32bisOrV32);
    EXPECT_TRUE(d.protocol.has_value());
    EXPECT_EQ(static_cast<int>(d.protocol.value()), static_cast<int>(V8Protocol::Lapm));
    EXPECT_TRUE(d.pstnAccess.has_value());
    EXPECT_TRUE(d.pstnAccess.value().digitalNetwork);
}

TEST(V8CallMenu_Decode_MissingCallFunctionFirstFails) {
    const std::vector<std::uint8_t> octets = {EncodePstnAccessOctet(V8PstnAccess{})};
    size_t index = 0;
    const auto decoded = DecodeCallMenu(octets, index);
    EXPECT_TRUE(!decoded.has_value());
    EXPECT_EQ(index, static_cast<size_t>(0));
}

TEST(V8CallMenu_Decode_StopsAtSequenceEndInLargerBuffer) {
    // Simulates a CM octet stream followed by the next sequence's
    // preamble bytes (which would never decode as a category octet).
    V8CallMenu menu;
    menu.callFunction = V8CallFunction::Data;
    std::vector<std::uint8_t> octets = EncodeCallMenu(menu);
    octets.push_back(0xFF); // not a category octet (b4 would be 1 -> extension, orphaned)

    size_t index = 0;
    const auto decoded = DecodeCallMenu(octets, index);
    EXPECT_TRUE(decoded.has_value());
    EXPECT_EQ(index, octets.size() - 1); // stopped before the trailing orphan byte
}

TEST(V8CallMenu_IntersectModulationModes_OnlyKeepsCommonModes) {
    V8ModulationModes a;
    a.v34Duplex = true;
    a.v32bisOrV32 = true;
    a.v21 = true;

    V8ModulationModes b;
    b.v34Duplex = true;
    b.v32bisOrV32 = false;
    b.v21 = true;

    const V8ModulationModes intersected = IntersectModulationModes(a, b);
    EXPECT_TRUE(intersected.v34Duplex);
    EXPECT_TRUE(!intersected.v32bisOrV32);
    EXPECT_TRUE(intersected.v21);
    EXPECT_TRUE(!intersected.pcmModemAvailabilityPresent);
}

TEST(V8CallMenu_SelectModulationMode_PicksLowestItemNumber) {
    V8ModulationModes modes;
    modes.v21 = true;
    modes.v32bisOrV32 = true; // item 3, lower than V21's item 12
    modes.v26bis = true;      // item 9

    const auto selected = SelectModulationMode(modes);
    EXPECT_TRUE(selected.has_value());
    EXPECT_EQ(static_cast<int>(selected.value()), static_cast<int>(V8ModulationModeItem::V32bisOrV32));
}

TEST(V8CallMenu_SelectModulationMode_NoneSetReturnsNullopt) {
    const V8ModulationModes modes;
    const auto selected = SelectModulationMode(modes);
    EXPECT_TRUE(!selected.has_value());
}
