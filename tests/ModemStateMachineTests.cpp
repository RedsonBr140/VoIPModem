#include "TestFramework.h"
#include "FakeCallController.h"
#include "../src/Lib/Core/ModemStateMachine.h"
#include "../src/Lib/Hayes/AtCommandParser.h"
#include "../src/Lib/Logging/Logger.h"

using voipmodem::AtCommandParser;
using voipmodem::CallOutcome;
using voipmodem::Logger;
using voipmodem::ModemStateMachine;
using voipmodem::ResultCode;
using voipmodem::test::FakeCallController;

TEST(BareAt_RespondsOk) {
    FakeCallController controller;
    Logger logger;
    ModemStateMachine core(controller, logger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("AT"));
    EXPECT_TRUE(outcome.hasResult);
    EXPECT_TRUE(outcome.finalResult == ResultCode::OK);
    EXPECT_TRUE(!outcome.suppressFinalResult);
}

TEST(InvalidCommand_RespondsError) {
    FakeCallController controller;
    Logger logger;
    ModemStateMachine core(controller, logger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("ATX1"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::Error);
}

TEST(BlankLine_HasNoResult) {
    FakeCallController controller;
    Logger logger;
    ModemStateMachine core(controller, logger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("   "));
    EXPECT_TRUE(!outcome.hasResult);
}

TEST(AtI_ReturnsIdentificationAsInfoLine) {
    FakeCallController controller;
    Logger logger;
    ModemStateMachine core(controller, logger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("ATI"));
    EXPECT_EQ(outcome.infoLines.size(), static_cast<size_t>(1));
    EXPECT_EQ(outcome.infoLines[0], std::string("VoIPModem Test Modem"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::OK);
}

TEST(AtQ1_SuppressesFinalResult) {
    FakeCallController controller;
    Logger logger;
    ModemStateMachine core(controller, logger, "VoIPModem Test Modem");

    core.Execute(AtCommandParser::Parse("ATQ1"));
    const auto outcome = core.Execute(AtCommandParser::Parse("AT"));
    EXPECT_TRUE(outcome.suppressFinalResult);
}

TEST(SetThenQueryRegister_RoundTrips) {
    FakeCallController controller;
    Logger logger;
    ModemStateMachine core(controller, logger, "VoIPModem Test Modem");

    core.Execute(AtCommandParser::Parse("ATS0=3"));
    const auto outcome = core.Execute(AtCommandParser::Parse("ATS0?"));
    EXPECT_EQ(outcome.infoLines.size(), static_cast<size_t>(1));
    EXPECT_EQ(outcome.infoLines[0], std::string("003"));
}

TEST(Dial_ReturnsControllerOutcomeAsResultCode) {
    FakeCallController controller;
    controller.dialOutcome = CallOutcome::Busy;
    Logger logger;
    ModemStateMachine core(controller, logger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("ATD5551234"));
    EXPECT_EQ(controller.lastDialedNumber, std::string("5551234"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::Busy);
}

TEST(DialWithToneModifier_DialsTheNumberNotTheLetterT) {
    // Regression test: ATDT5551234 must dial "5551234", not "T5551234" or
    // just "T" — Windows modem drivers send ATDT by default.
    FakeCallController controller;
    Logger logger;
    ModemStateMachine core(controller, logger, "VoIPModem Test Modem");

    core.Execute(AtCommandParser::Parse("ATDT5551234"));
    EXPECT_EQ(controller.lastDialedNumber, std::string("5551234"));
}

TEST(DialWithPulseModifier_DialsTheNumberNotTheLetterP) {
    FakeCallController controller;
    Logger logger;
    ModemStateMachine core(controller, logger, "VoIPModem Test Modem");

    core.Execute(AtCommandParser::Parse("ATDP5551234"));
    EXPECT_EQ(controller.lastDialedNumber, std::string("5551234"));
}

TEST(Answer_ReturnsControllerOutcomeAsResultCode) {
    FakeCallController controller;
    controller.answerOutcome = CallOutcome::NoAnswer;
    Logger logger;
    ModemStateMachine core(controller, logger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("ATA"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::NoAnswer);
}

TEST(HangUp_InvokesController) {
    FakeCallController controller;
    Logger logger;
    ModemStateMachine core(controller, logger, "VoIPModem Test Modem");

    core.Execute(AtCommandParser::Parse("ATH"));
    EXPECT_EQ(controller.hangUpCount, 1);
}

TEST(AmpersandF_RestoresDefaultsAfterChanges) {
    FakeCallController controller;
    Logger logger;
    ModemStateMachine core(controller, logger, "VoIPModem Test Modem");

    core.Execute(AtCommandParser::Parse("ATE0V0Q1"));
    core.Execute(AtCommandParser::Parse("AT&F"));

    EXPECT_TRUE(core.Profile().echoEnabled);
    EXPECT_TRUE(core.Profile().verboseResultCodes);
    EXPECT_TRUE(!core.Profile().quietMode);
}
