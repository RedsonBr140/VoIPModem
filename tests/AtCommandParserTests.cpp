#include "TestFramework.h"
#include "../src/Lib/Hayes/AtCommandParser.h"

using voipmodem::AtCommandParser;
using voipmodem::AtCommandType;

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
    EXPECT_EQ(result.commands[0].stringParam, std::string("5551234"));
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

TEST(AmpersandUnknownLetter_IsInvalid) {
    const auto result = AtCommandParser::Parse("AT&Z");
    EXPECT_TRUE(!result.isValid);
}

TEST(UnknownLetterCommand_IsInvalid) {
    const auto result = AtCommandParser::Parse("ATX1");
    EXPECT_TRUE(!result.isValid);
}
