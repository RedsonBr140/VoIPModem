#include "../src/Lib/Dsp/V8Message.h"
#include "TestFramework.h"

using namespace voipmodem::dsp;

// Every expected bit pattern below is copied from the actual ITU-T
// Recommendation V.8 (11/2000) text (clauses 5 and 6), not guessed --
// see docs/ROADMAP.md Phase 4 and V8Message.h's header comment.

TEST(V8_MakeCategoryOctet_CallFunctionTag_MatchesSpecTable3Prefix) {
    // Table 3: "0 1000 ... 0 ..." -- start=0, b0b1b2b3=1,0,0,0, b4=0.
    // b0-b3 as a 4-bit number (b0 = LSB) is 1, matching V8CategoryTag::CallFunction.
    const std::uint8_t octet = MakeCategoryOctet(V8CategoryTag::CallFunction, false, false, false);
    EXPECT_EQ(static_cast<int>(octet & 0x1F), 0x01); // tag=1, b4=0, b5=0
    EXPECT_TRUE(IsCategoryOctet(octet));
    EXPECT_TRUE(!IsExtensionOctet(octet));
    EXPECT_TRUE(GetCategoryTag(octet).has_value());
    EXPECT_EQ(static_cast<int>(GetCategoryTag(octet).value()), static_cast<int>(V8CategoryTag::CallFunction));
}

TEST(V8_MakeExtensionOctet_SetsFixedBitsPerSpec) {
    // 5.2: b3=0, b4=1, b5=0 always, regardless of option bits.
    const std::uint8_t octet = MakeExtensionOctet(true, true, true, true, true);
    EXPECT_TRUE(!((octet >> 3) & 0x01));
    EXPECT_TRUE(((octet >> 4) & 0x01) != 0);
    EXPECT_TRUE(!((octet >> 5) & 0x01));
    EXPECT_TRUE(IsExtensionOctet(octet));
    EXPECT_TRUE(!IsCategoryOctet(octet));
}

TEST(V8_CallFunction_DataRoundTrips) {
    // Table 3: "0 1 1  Data (unspecified application)" -- b5=0,b6=1,b7=1.
    const std::uint8_t octet = EncodeCallFunctionOctet(V8CallFunction::Data);
    EXPECT_TRUE(!((octet >> 5) & 0x01));
    EXPECT_TRUE(((octet >> 6) & 0x01) != 0);
    EXPECT_TRUE(((octet >> 7) & 0x01) != 0);

    const auto decoded = DecodeCallFunctionOctet(octet);
    EXPECT_TRUE(decoded.has_value());
    EXPECT_EQ(static_cast<int>(decoded.value()), static_cast<int>(V8CallFunction::Data));
}

TEST(V8_CallFunction_AllEightValuesRoundTrip) {
    const V8CallFunction allFunctions[] = {
        V8CallFunction::ToBeDetermined,     V8CallFunction::PstnMultimediaTerminal, V8CallFunction::Textphone,
        V8CallFunction::Videotext,          V8CallFunction::TransmitFacsimile,      V8CallFunction::ReceiveFacsimile,
        V8CallFunction::Data,               V8CallFunction::IndicatedInExtensionOctet};
    for (V8CallFunction function : allFunctions) {
        const std::uint8_t octet = EncodeCallFunctionOctet(function);
        const auto decoded = DecodeCallFunctionOctet(octet);
        EXPECT_TRUE(decoded.has_value());
        EXPECT_EQ(static_cast<int>(decoded.value()), static_cast<int>(function));
    }
}

TEST(V8_CallFunction_DecodeRejectsWrongTag) {
    const std::uint8_t modulationModesOctet = MakeCategoryOctet(V8CategoryTag::ModulationModes, false, false, false);
    const auto decoded = DecodeCallFunctionOctet(modulationModesOctet);
    EXPECT_TRUE(!decoded.has_value());
}

TEST(V8_ModulationModes_OnlyModn0WhenNothingElseSet) {
    V8ModulationModes modes;
    modes.v34Duplex = true;

    const std::vector<std::uint8_t> octets = EncodeModulationModesOctets(modes);
    EXPECT_EQ(octets.size(), static_cast<size_t>(1));

    size_t index = 0;
    const auto decoded = DecodeModulationModesOctets(octets, index);
    EXPECT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded.value().v34Duplex);
    EXPECT_TRUE(!decoded.value().v34HalfDuplex);
    EXPECT_EQ(index, static_cast<size_t>(1));
}

TEST(V8_ModulationModes_V21OnlyStillEmitsModn1AsPositionalPlaceholder) {
    // V21 lives in modn2 (the *second* extension octet); modn1 must still
    // be emitted, all-zero, purely so modn2 lands in the right position.
    V8ModulationModes modes;
    modes.v21 = true;

    const std::vector<std::uint8_t> octets = EncodeModulationModesOctets(modes);
    EXPECT_EQ(octets.size(), static_cast<size_t>(3));
    EXPECT_TRUE(IsExtensionOctet(octets[1]));
    EXPECT_TRUE(IsExtensionOctet(octets[2]));

    size_t index = 0;
    const auto decoded = DecodeModulationModesOctets(octets, index);
    EXPECT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded.value().v21);
    EXPECT_TRUE(!decoded.value().v27ter);
    EXPECT_EQ(index, static_cast<size_t>(3));
}

TEST(V8_ModulationModes_AllFieldsRoundTrip) {
    V8ModulationModes modes;
    modes.pcmModemAvailabilityPresent = true;
    modes.v34Duplex = true;
    modes.v34HalfDuplex = false;
    modes.v32bisOrV32 = true;
    modes.v22bisOrV22 = false;
    modes.v17 = true;
    modes.v29HalfDuplex = false;
    modes.v27ter = true;
    modes.v26ter = true;
    modes.v26bis = false;
    modes.v23Duplex = true;
    modes.v23HalfDuplex = false;
    modes.v21 = true;

    const std::vector<std::uint8_t> octets = EncodeModulationModesOctets(modes);
    EXPECT_EQ(octets.size(), static_cast<size_t>(3));

    size_t index = 0;
    const auto decoded = DecodeModulationModesOctets(octets, index);
    EXPECT_TRUE(decoded.has_value());
    const V8ModulationModes& d = decoded.value();
    EXPECT_TRUE(d.pcmModemAvailabilityPresent);
    EXPECT_TRUE(d.v34Duplex);
    EXPECT_TRUE(!d.v34HalfDuplex);
    EXPECT_TRUE(d.v32bisOrV32);
    EXPECT_TRUE(!d.v22bisOrV22);
    EXPECT_TRUE(d.v17);
    EXPECT_TRUE(!d.v29HalfDuplex);
    EXPECT_TRUE(d.v27ter);
    EXPECT_TRUE(d.v26ter);
    EXPECT_TRUE(!d.v26bis);
    EXPECT_TRUE(d.v23Duplex);
    EXPECT_TRUE(!d.v23HalfDuplex);
    EXPECT_TRUE(d.v21);
}

TEST(V8_ModulationModes_DecodeAdvancesPastSequenceOnlyInLargerBuffer) {
    // Simulates CM: callf0 followed by modn0/modn1/modn2, nothing after.
    V8ModulationModes modes;
    modes.v21 = true;
    std::vector<std::uint8_t> octets = {EncodeCallFunctionOctet(V8CallFunction::Data)};
    const std::vector<std::uint8_t> modOctets = EncodeModulationModesOctets(modes);
    octets.insert(octets.end(), modOctets.begin(), modOctets.end());

    size_t index = 1; // skip callf0
    const auto decoded = DecodeModulationModesOctets(octets, index);
    EXPECT_TRUE(decoded.has_value());
    EXPECT_EQ(index, octets.size());
}

TEST(V8_PcmModemAvailability_RoundTrips) {
    V8PcmModemAvailability pcm;
    pcm.v90OrV92Analogue = true;
    pcm.v90OrV92Digital = false;
    pcm.v91 = true;

    const std::uint8_t octet = EncodePcmModemAvailabilityOctet(pcm);
    const auto decoded = DecodePcmModemAvailabilityOctet(octet);
    EXPECT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded.value().v90OrV92Analogue);
    EXPECT_TRUE(!decoded.value().v90OrV92Digital);
    EXPECT_TRUE(decoded.value().v91);
}

TEST(V8_Protocol_LapmRoundTrips) {
    // Table 6: "1 0 0  Calls for LAPM protocol according to ITU-T V.42".
    const std::uint8_t octet = EncodeProtocolOctet(V8Protocol::Lapm);
    EXPECT_TRUE(((octet >> 5) & 0x01) != 0);
    EXPECT_TRUE(!((octet >> 6) & 0x01));
    EXPECT_TRUE(!((octet >> 7) & 0x01));

    const auto decoded = DecodeProtocolOctet(octet);
    EXPECT_TRUE(decoded.has_value());
    EXPECT_EQ(static_cast<int>(decoded.value()), static_cast<int>(V8Protocol::Lapm));
}

TEST(V8_Protocol_UndefinedBitPatternDecodesToNullopt) {
    // Only "100" (LAPM) and "111" (extension) are defined in Table 6;
    // "000" is not a valid prot0 value at all.
    const std::uint8_t octet = MakeCategoryOctet(V8CategoryTag::Protocols, false, false, false);
    const auto decoded = DecodeProtocolOctet(octet);
    EXPECT_TRUE(!decoded.has_value());
}

TEST(V8_PstnAccess_RoundTrips) {
    // 7.3: digital network -> b7=1; analogue -> b7=0.
    V8PstnAccess access;
    access.callDceCellular = true;
    access.answerDceCellular = false;
    access.digitalNetwork = true;

    const std::uint8_t octet = EncodePstnAccessOctet(access);
    const auto decoded = DecodePstnAccessOctet(octet);
    EXPECT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded.value().callDceCellular);
    EXPECT_TRUE(!decoded.value().answerDceCellular);
    EXPECT_TRUE(decoded.value().digitalNetwork);
}

TEST(V8_BuildCjOctets_IsThreeZeroOctets) {
    // 3.5: "three consecutive octets of all ZEROs with start and stop bits".
    const std::vector<std::uint8_t> cj = BuildCjOctets();
    EXPECT_EQ(cj.size(), static_cast<size_t>(3));
    for (std::uint8_t octet : cj) {
        EXPECT_EQ(static_cast<int>(octet), 0);
    }
}

TEST(V8_Preamble_CiAndCmJmSyncDifferAsSpecified) {
    // Table 1: CI sync is 0000000001, CM/JM sync is 0000001111 -- distinct
    // patterns so a receiver can tell which kind of sequence is starting.
    bool anyDifferent = false;
    for (int i = 0; i < 10; ++i) {
        if (kV8CiSyncBits[i] != kV8CmJmSyncBits[i]) {
            anyDifferent = true;
        }
    }
    EXPECT_TRUE(anyDifferent);
    EXPECT_TRUE(!kV8CiSyncBits[9] ? false : true); // CI sync ends in a ONE per Table 1
}
