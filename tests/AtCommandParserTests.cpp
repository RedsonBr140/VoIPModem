#include "TestFramework.h"
#include "../src/Lib/Hayes/AtCommandParser.h"

using voipmodem::AtCommandParser;
using voipmodem::AtCommandType;
using voipmodem::AtExtendedSuffix;

TEST(BareAt_IsAttentionOnly) {
    const auto result = AtCommandParser::Parse("AT");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands.size(), static_cast<size_t>(1));
    EXPECT_TRUE(result.commands[0].type == AtCommandType::Attention);
}

TEST(LowercaseAt_IsAccepted) {
    const auto result = AtCommandParser::Parse("at");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands.size(), static_cast<size_t>(1));
}

TEST(MissingAtPrefix_IsInvalid) {
    const auto result = AtCommandParser::Parse("HELLO");
    EXPECT_TRUE(!result.isValid);
}

TEST(BlankLine_IsNoOpNotError) {
    const auto result = AtCommandParser::Parse("   ");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands.empty());
}

TEST(AtI_IsIdentify) {
    const auto result = AtCommandParser::Parse("ATI");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands.size(), static_cast<size_t>(1));
    EXPECT_TRUE(result.commands[0].type == AtCommandType::Identify);
}

TEST(AtZ_IsReset) {
    const auto result = AtCommandParser::Parse("ATZ");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::Reset);
}

TEST(ChainedCommands_AllParsed) {
    const auto result = AtCommandParser::Parse("ATE1V0Q0");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands.size(), static_cast<size_t>(3));
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetEcho);
    EXPECT_EQ(result.commands[0].numericParam.value_or(-1), 1);
    EXPECT_TRUE(result.commands[1].type == AtCommandType::SetVerbose);
    EXPECT_EQ(result.commands[1].numericParam.value_or(-1), 0);
    EXPECT_TRUE(result.commands[2].type == AtCommandType::SetQuiet);
    EXPECT_EQ(result.commands[2].numericParam.value_or(-1), 0);
}

TEST(AtL_IsSetSpeakerVolume) {
    const auto result = AtCommandParser::Parse("ATL1");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetSpeakerVolume);
    EXPECT_EQ(result.commands[0].numericParam.value_or(-1), 1);
}

TEST(AtM_IsSetSpeakerMode) {
    const auto result = AtCommandParser::Parse("ATM0");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetSpeakerMode);
    EXPECT_EQ(result.commands[0].numericParam.value_or(-1), 0);
}

TEST(AtLAndAtM_ChainWithOtherCommands) {
    // A common real-world init string shape (Windows' generic "Standard
    // Modem Types" driver uses something like this).
    const auto result = AtCommandParser::Parse("ATL2M1E0V1");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands.size(), static_cast<size_t>(4));
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetSpeakerVolume);
    EXPECT_EQ(result.commands[0].numericParam.value_or(-1), 2);
    EXPECT_TRUE(result.commands[1].type == AtCommandType::SetSpeakerMode);
    EXPECT_EQ(result.commands[1].numericParam.value_or(-1), 1);
}

TEST(EchoWithoutDigit_DefaultsToZero) {
    const auto result = AtCommandParser::Parse("ATE");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(!result.commands[0].numericParam.has_value());
}

TEST(AtD_CapturesDialStringToEndOfLine) {
    const auto result = AtCommandParser::Parse("ATD5551234");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands.size(), static_cast<size_t>(1));
    EXPECT_TRUE(result.commands[0].type == AtCommandType::Dial);
    EXPECT_TRUE(result.commands[0].dialMode == voipmodem::AtDialMode::Default);
    EXPECT_EQ(result.commands[0].stringParam, std::string("5551234"));
}

TEST(AtDT_StripsToneModifierFromDialString) {
    const auto result = AtCommandParser::Parse("ATDT5551234");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::Dial);
    EXPECT_TRUE(result.commands[0].dialMode == voipmodem::AtDialMode::Tone);
    EXPECT_EQ(result.commands[0].stringParam, std::string("5551234"));
}

TEST(AtDP_StripsPulseModifierFromDialString) {
    const auto result = AtCommandParser::Parse("ATDP5551234");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::Dial);
    EXPECT_TRUE(result.commands[0].dialMode == voipmodem::AtDialMode::Pulse);
    EXPECT_EQ(result.commands[0].stringParam, std::string("5551234"));
}

TEST(AtDT_Lowercase_StillStripsModifier) {
    const auto result = AtCommandParser::Parse("atdt5551234");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].dialMode == voipmodem::AtDialMode::Tone);
    EXPECT_EQ(result.commands[0].stringParam, std::string("5551234"));
}

TEST(AtDT_WithNoNumber_YieldsEmptyDialString) {
    const auto result = AtCommandParser::Parse("ATDT");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].dialMode == voipmodem::AtDialMode::Tone);
    EXPECT_EQ(result.commands[0].stringParam, std::string(""));
}

TEST(AtA_IsAnswer) {
    const auto result = AtCommandParser::Parse("ATA");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::Answer);
}

TEST(AtH_IsHangUp) {
    const auto result = AtCommandParser::Parse("ATH");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::HangUp);
}

TEST(SetRegister_ParsesIndexAndValue) {
    const auto result = AtCommandParser::Parse("ATS0=2");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetRegister);
    EXPECT_EQ(result.commands[0].registerIndex.value_or(-1), 0);
    EXPECT_EQ(result.commands[0].numericParam.value_or(-1), 2);
}

TEST(QueryRegister_ParsesIndex) {
    const auto result = AtCommandParser::Parse("ATS0?");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::QueryRegister);
    EXPECT_EQ(result.commands[0].registerIndex.value_or(-1), 0);
}

TEST(RegisterWithoutEqualsOrQuery_IsInvalid) {
    const auto result = AtCommandParser::Parse("ATS0");
    EXPECT_TRUE(!result.isValid);
}

TEST(AmpersandF_IsFactoryDefaults) {
    const auto result = AtCommandParser::Parse("AT&F");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::FactoryDefaults);
}

TEST(ExtendedCommand_TestSuffix_ParsesNameAndSuffix) {
    const auto result = AtCommandParser::Parse("AT+FCLASS=?");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands.size(), static_cast<size_t>(1));
    EXPECT_TRUE(result.commands[0].type == AtCommandType::ExtendedCommand);
    EXPECT_EQ(result.commands[0].extendedPrefix, '+');
    EXPECT_EQ(result.commands[0].extendedName, std::string("FCLASS"));
    EXPECT_TRUE(result.commands[0].extendedSuffix == AtExtendedSuffix::Test);
}

TEST(ExtendedCommand_QuerySuffix_Parses) {
    const auto result = AtCommandParser::Parse("AT+GCI?");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands[0].extendedName, std::string("GCI"));
    EXPECT_TRUE(result.commands[0].extendedSuffix == AtExtendedSuffix::Query);
}

TEST(ExtendedCommand_SetValueSuffix_CapturesValue) {
    const auto result = AtCommandParser::Parse("AT+FCLASS=0");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].extendedSuffix == AtExtendedSuffix::SetValue);
    EXPECT_EQ(result.commands[0].extendedValue, std::string("0"));
}

TEST(ExtendedCommand_NoSuffix_IsBareAction) {
    const auto result = AtCommandParser::Parse("AT+GMM");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands[0].extendedName, std::string("GMM"));
    EXPECT_TRUE(result.commands[0].extendedSuffix == AtExtendedSuffix::None);
}

TEST(ExtendedCommand_PoundPrefix_Parses) {
    const auto result = AtCommandParser::Parse("AT#CLS=?");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands[0].extendedPrefix, '#');
    EXPECT_EQ(result.commands[0].extendedName, std::string("CLS"));
    EXPECT_TRUE(result.commands[0].extendedSuffix == AtExtendedSuffix::Test);
}

TEST(ExtendedCommand_NoNameAfterPrefix_IsInvalid) {
    const auto result = AtCommandParser::Parse("AT+");
    EXPECT_TRUE(!result.isValid);
}

TEST(AmpersandUnknownLetter_IsInvalid) {
    // &Z (store a phone number on Hayes modems) is still unsupported.
    const auto result = AtCommandParser::Parse("AT&Z");
    EXPECT_TRUE(!result.isValid);
}

TEST(BareAmpersand_IsInvalid) {
    const auto result = AtCommandParser::Parse("AT&");
    EXPECT_TRUE(!result.isValid);
}

TEST(UnknownLetterCommand_IsInvalid) {
    // Y (long-space disconnect on Hayes modems) is still unsupported.
    // This used ATX1 until X became a recognised command.
    const auto result = AtCommandParser::Parse("ATY1");
    EXPECT_TRUE(!result.isValid);
}

// --- Commands sent by Windows' generic modem driver (Unimodem) ---------

TEST(AmpersandC_WithDigit_IsSetDcdMode) {
    const auto result = AtCommandParser::Parse("AT&C1");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands.size(), static_cast<size_t>(1));
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetDcdMode);
    EXPECT_EQ(result.commands[0].numericParam.value_or(-1), 1);
}

TEST(AmpersandC_WithoutDigit_HasNoParam) {
    const auto result = AtCommandParser::Parse("AT&C");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetDcdMode);
    EXPECT_TRUE(!result.commands[0].numericParam.has_value());
}

TEST(AmpersandC_OutOfRange_IsInvalid) {
    EXPECT_TRUE(AtCommandParser::Parse("AT&C0").isValid);
    EXPECT_TRUE(!AtCommandParser::Parse("AT&C2").isValid);
}

TEST(AmpersandD_WithDigit_IsSetDtrMode) {
    const auto result = AtCommandParser::Parse("AT&D2");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetDtrMode);
    EXPECT_EQ(result.commands[0].numericParam.value_or(-1), 2);
}

TEST(AmpersandD_WithoutDigit_HasNoParam) {
    const auto result = AtCommandParser::Parse("AT&D");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetDtrMode);
    EXPECT_TRUE(!result.commands[0].numericParam.has_value());
}

TEST(AmpersandD_ZeroToThreeValid_FourIsInvalid) {
    EXPECT_TRUE(AtCommandParser::Parse("AT&D0").isValid);
    EXPECT_TRUE(AtCommandParser::Parse("AT&D1").isValid);
    EXPECT_TRUE(AtCommandParser::Parse("AT&D3").isValid);
    EXPECT_TRUE(!AtCommandParser::Parse("AT&D4").isValid);
}

TEST(AmpersandK_WithDigit_IsSetFlowControl) {
    const auto result = AtCommandParser::Parse("AT&K3");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetFlowControl);
    EXPECT_EQ(result.commands[0].numericParam.value_or(-1), 3);
}

TEST(AmpersandK_WithoutDigit_HasNoParam) {
    const auto result = AtCommandParser::Parse("AT&K");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetFlowControl);
    EXPECT_TRUE(!result.commands[0].numericParam.has_value());
}

TEST(AmpersandK_OnlyZeroAndThreeToSixAreValid) {
    EXPECT_TRUE(AtCommandParser::Parse("AT&K0").isValid);
    EXPECT_TRUE(!AtCommandParser::Parse("AT&K1").isValid);
    EXPECT_TRUE(!AtCommandParser::Parse("AT&K2").isValid);
    EXPECT_TRUE(AtCommandParser::Parse("AT&K4").isValid);
    EXPECT_TRUE(AtCommandParser::Parse("AT&K5").isValid);
    EXPECT_TRUE(AtCommandParser::Parse("AT&K6").isValid);
    EXPECT_TRUE(!AtCommandParser::Parse("AT&K7").isValid);
}

TEST(AtX_WithDigit_IsSetResultCodeSet) {
    const auto result = AtCommandParser::Parse("ATX4");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetResultCodeSet);
    EXPECT_EQ(result.commands[0].numericParam.value_or(-1), 4);
}

TEST(AtX_WithoutDigit_HasNoParam) {
    const auto result = AtCommandParser::Parse("ATX");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetResultCodeSet);
    EXPECT_TRUE(!result.commands[0].numericParam.has_value());
}

TEST(AtX_OutOfRange_IsInvalid) {
    EXPECT_TRUE(AtCommandParser::Parse("ATX0").isValid);
    EXPECT_TRUE(!AtCommandParser::Parse("ATX5").isValid);
}

TEST(AtN_WithDigit_IsSetSpeedNegotiation) {
    const auto result = AtCommandParser::Parse("ATN1");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetSpeedNegotiation);
    EXPECT_EQ(result.commands[0].numericParam.value_or(-1), 1);
}

TEST(AtN_WithoutDigit_HasNoParam) {
    const auto result = AtCommandParser::Parse("ATN");
    EXPECT_TRUE(result.isValid);
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetSpeedNegotiation);
    EXPECT_TRUE(!result.commands[0].numericParam.has_value());
}

TEST(AtN_OutOfRange_IsInvalid) {
    EXPECT_TRUE(AtCommandParser::Parse("ATN0").isValid);
    EXPECT_TRUE(!AtCommandParser::Parse("ATN2").isValid);
}

TEST(NewSettingCommands_LowercaseAndChained_AreAccepted) {
    const auto result = AtCommandParser::Parse("at&c1&d2&k3n1x4");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands.size(), static_cast<size_t>(5));
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetDcdMode);
    EXPECT_TRUE(result.commands[1].type == AtCommandType::SetDtrMode);
    EXPECT_TRUE(result.commands[2].type == AtCommandType::SetFlowControl);
    EXPECT_TRUE(result.commands[3].type == AtCommandType::SetSpeedNegotiation);
    EXPECT_TRUE(result.commands[4].type == AtCommandType::SetResultCodeSet);
}

TEST(NewSettingCommands_WithoutDigits_Chain) {
    const auto result = AtCommandParser::Parse("AT&C&D&KNXE0");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands.size(), static_cast<size_t>(6));
    EXPECT_TRUE(result.commands[4].type == AtCommandType::SetResultCodeSet);
    EXPECT_TRUE(result.commands[5].type == AtCommandType::SetEcho);
}

TEST(OutOfRangeInChain_InvalidatesWholeLine) {
    EXPECT_TRUE(!AtCommandParser::Parse("ATE0&C1&D9V1").isValid);
}

TEST(UnimodemInitString1_ParsesInOrder) {
    // mdmgen.inf, "Standard 56000 bps Modem" (Gen556F), Windows Server 2022.
    const auto result = AtCommandParser::Parse("AT&FE0V1&C1&D2S95=47S0=0");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands.size(), static_cast<size_t>(7));
    EXPECT_TRUE(result.commands[0].type == AtCommandType::FactoryDefaults);
    EXPECT_TRUE(result.commands[1].type == AtCommandType::SetEcho);
    EXPECT_TRUE(result.commands[2].type == AtCommandType::SetVerbose);
    EXPECT_TRUE(result.commands[3].type == AtCommandType::SetDcdMode);
    EXPECT_EQ(result.commands[3].numericParam.value_or(-1), 1);
    EXPECT_TRUE(result.commands[4].type == AtCommandType::SetDtrMode);
    EXPECT_EQ(result.commands[4].numericParam.value_or(-1), 2);
    EXPECT_TRUE(result.commands[5].type == AtCommandType::SetRegister);
    EXPECT_EQ(result.commands[5].registerIndex.value_or(-1), 95);
    EXPECT_TRUE(result.commands[6].type == AtCommandType::SetRegister);
    EXPECT_EQ(result.commands[6].registerIndex.value_or(-1), 0);
}

TEST(UnimodemInitString2_ParsesInOrder) {
    const auto result = AtCommandParser::Parse("ATS7=60L0M1&K3N1X4");
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.commands.size(), static_cast<size_t>(6));
    EXPECT_TRUE(result.commands[0].type == AtCommandType::SetRegister);
    EXPECT_EQ(result.commands[0].registerIndex.value_or(-1), 7);
    EXPECT_EQ(result.commands[0].numericParam.value_or(-1), 60);
    EXPECT_TRUE(result.commands[1].type == AtCommandType::SetSpeakerVolume);
    EXPECT_TRUE(result.commands[2].type == AtCommandType::SetSpeakerMode);
    EXPECT_TRUE(result.commands[3].type == AtCommandType::SetFlowControl);
    EXPECT_EQ(result.commands[3].numericParam.value_or(-1), 3);
    EXPECT_TRUE(result.commands[4].type == AtCommandType::SetSpeedNegotiation);
    EXPECT_EQ(result.commands[4].numericParam.value_or(-1), 1);
    EXPECT_TRUE(result.commands[5].type == AtCommandType::SetResultCodeSet);
    EXPECT_EQ(result.commands[5].numericParam.value_or(-1), 4);
}
