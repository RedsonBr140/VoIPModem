#include "TestFramework.h"
#include "FakeCallController.h"
#include "../src/Lib/Core/ModemStateMachine.h"
#include "../src/Lib/Hayes/AtCommandParser.h"
#include "../src/Lib/Logging/Logger.h"
#include "../src/Lib/Logging/TaggedLogger.h"

using voipmodem::AtCommandParser;
using voipmodem::CallEvent;
using voipmodem::CallEventType;
using voipmodem::Logger;
using voipmodem::ModemState;
using voipmodem::ModemStateMachine;
using voipmodem::ResultCode;
using voipmodem::TaggedLogger;
using voipmodem::test::FakeCallController;

TEST(BareAt_RespondsOk) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("AT"));
    EXPECT_TRUE(outcome.hasResult);
    EXPECT_TRUE(outcome.finalResult == ResultCode::OK);
    EXPECT_TRUE(!outcome.suppressFinalResult);
}

TEST(InvalidCommand_RespondsError) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    // Y is still unsupported; this used ATX1 until X became a recognised command.
    const auto outcome = core.Execute(AtCommandParser::Parse("ATY1"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::Error);
}

TEST(BlankLine_HasNoResult) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("   "));
    EXPECT_TRUE(!outcome.hasResult);
}

TEST(AtI_ReturnsIdentificationAsInfoLine) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("ATI"));
    EXPECT_EQ(outcome.infoLines.size(), static_cast<size_t>(1));
    EXPECT_EQ(outcome.infoLines[0], std::string("VoIPModem Test Modem"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::OK);
}

TEST(AtQ1_SuppressesFinalResult) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    core.Execute(AtCommandParser::Parse("ATQ1"));
    const auto outcome = core.Execute(AtCommandParser::Parse("AT"));
    EXPECT_TRUE(outcome.suppressFinalResult);
}

TEST(SetThenQueryRegister_RoundTrips) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    core.Execute(AtCommandParser::Parse("ATS0=3"));
    const auto outcome = core.Execute(AtCommandParser::Parse("ATS0?"));
    EXPECT_EQ(outcome.infoLines.size(), static_cast<size_t>(1));
    EXPECT_EQ(outcome.infoLines[0], std::string("003"));
}

TEST(S0Register_DefaultsToZero_AutoAnswerDisabled) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    EXPECT_EQ(core.Registers().Get(0), 0);
}

TEST(InitializeAutoAnswerRegister_SeedsS0FromConfig) {
    // Regression test: ModemInstanceConfig::autoAnswerRingCount was parsed
    // from the INI but never actually reached the S0 register, so
    // AutoAnswerRingCount=N in config silently did nothing unless the user
    // manually typed ATS0=N over the serial line.
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    core.InitializeAutoAnswerRegister(2);
    EXPECT_EQ(core.Registers().Get(0), 2);
}

TEST(Dial_HasNoImmediateResult) {
    // Real call setup resolves later (see ICallController); Execute()
    // itself must not print CONNECT/NO CARRIER/etc. immediately.
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("ATD5551234"));
    EXPECT_TRUE(!outcome.hasResult);
    EXPECT_EQ(controller.lastDialedNumber, std::string("5551234"));
}

TEST(Dial_ExposesLastDialedNumber_WithToneModifierStripped) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    core.Execute(AtCommandParser::Parse("ATDT5551234"));
    EXPECT_EQ(core.LastDialedNumber(), std::string("5551234"));
    EXPECT_TRUE(core.CurrentState() == ModemState::Dialing);
}

TEST(DialThenPollEvent_ReportsControllerOutcomeAsResultCode) {
    FakeCallController controller;
    controller.dialEvent = CallEvent{CallEventType::Ended, ResultCode::Busy};
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    core.Execute(AtCommandParser::Parse("ATD5551234"));
    const auto outcome = core.HandleCallEvent(controller.PollEvent());
    EXPECT_TRUE(outcome.finalResult == ResultCode::Busy);
}

TEST(HandleCallEvent_Connected_ReportsConnect) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.HandleCallEvent(CallEvent{CallEventType::Connected});
    EXPECT_TRUE(outcome.finalResult == ResultCode::Connect);
    EXPECT_EQ(outcome.resultSuffix, std::string()); // rate unknown: plain CONNECT
}

TEST(HandleCallEvent_ConnectedWithRate_ReportsConnectRate) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    CallEvent event{CallEventType::Connected};
    event.connectRateBps = 31200;
    const auto outcome = core.HandleCallEvent(event);
    EXPECT_TRUE(outcome.finalResult == ResultCode::Connect);
    EXPECT_EQ(outcome.resultSuffix, std::string(" 31200"));
    EXPECT_TRUE(core.CurrentState() == ModemState::OnlineData);
}

TEST(HandleCallEvent_IncomingCall_SetsHasIncomingCallAndNoResult) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    EXPECT_TRUE(!core.HasIncomingCall());
    const auto outcome = core.HandleCallEvent(CallEvent{CallEventType::IncomingCall});
    EXPECT_TRUE(!outcome.hasResult);
    EXPECT_TRUE(core.HasIncomingCall());
}

TEST(Answer_ClearsHasIncomingCall) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    core.HandleCallEvent(CallEvent{CallEventType::IncomingCall});
    EXPECT_TRUE(core.HasIncomingCall());

    core.Execute(AtCommandParser::Parse("ATA"));
    EXPECT_TRUE(!core.HasIncomingCall());
}

TEST(DialWithToneModifier_DialsTheNumberNotTheLetterT) {
    // Regression test: ATDT5551234 must dial "5551234", not "T5551234" or
    // just "T" — Windows modem drivers send ATDT by default.
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    core.Execute(AtCommandParser::Parse("ATDT5551234"));
    EXPECT_EQ(controller.lastDialedNumber, std::string("5551234"));
}

TEST(DialWithPulseModifier_DialsTheNumberNotTheLetterP) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    core.Execute(AtCommandParser::Parse("ATDP5551234"));
    EXPECT_EQ(controller.lastDialedNumber, std::string("5551234"));
}

TEST(AnswerThenPollEvent_ReportsControllerOutcomeAsResultCode) {
    FakeCallController controller;
    controller.answerEvent = CallEvent{CallEventType::Ended, ResultCode::NoAnswer};
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto execOutcome = core.Execute(AtCommandParser::Parse("ATA"));
    EXPECT_TRUE(!execOutcome.hasResult);
    const auto outcome = core.HandleCallEvent(controller.PollEvent());
    EXPECT_TRUE(outcome.finalResult == ResultCode::NoAnswer);
}

TEST(HangUp_InvokesController) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    core.Execute(AtCommandParser::Parse("ATH"));
    EXPECT_EQ(controller.hangUpCount, 1);
}

TEST(AmpersandF_RestoresDefaultsAfterChanges) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    core.Execute(AtCommandParser::Parse("ATE0V0Q1L0M0"));
    core.Execute(AtCommandParser::Parse("AT&F"));

    EXPECT_TRUE(core.Profile().echoEnabled);
    EXPECT_TRUE(core.Profile().verboseResultCodes);
    EXPECT_TRUE(!core.Profile().quietMode);
    EXPECT_EQ(core.Profile().speakerVolume, 2);
    EXPECT_EQ(core.Profile().speakerMode, 1);
}

TEST(AtL_SetsSpeakerVolume_ReturnsOk) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("ATL3"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::OK);
    EXPECT_EQ(core.Profile().speakerVolume, 3);
}

TEST(AtM_SetsSpeakerMode_ReturnsOk) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("ATM2"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::OK);
    EXPECT_EQ(core.Profile().speakerMode, 2);
}

TEST(UnimodemInitString1_ReturnsOk) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("AT&FE0V1&C1&D2S95=47S0=0"));
    EXPECT_TRUE(outcome.hasResult);
    EXPECT_TRUE(outcome.finalResult == ResultCode::OK);
    EXPECT_TRUE(!core.Profile().echoEnabled);
    EXPECT_EQ(core.Profile().dcdMode, 1);
    EXPECT_EQ(core.Profile().dtrMode, 2);
}

TEST(UnimodemInitString2_ReturnsOk) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("ATS7=60L0M1&K3N1X4"));
    EXPECT_TRUE(outcome.hasResult);
    EXPECT_TRUE(outcome.finalResult == ResultCode::OK);
    EXPECT_EQ(core.Profile().flowControl, 3);
    EXPECT_EQ(core.Profile().speedNegotiation, 1);
    EXPECT_EQ(core.Profile().resultCodeSet, 4);
}

TEST(NewSettingCommands_StoreValues) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("AT&C0&D3&K6X2N0"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::OK);
    EXPECT_EQ(core.Profile().dcdMode, 0);
    EXPECT_EQ(core.Profile().dtrMode, 3);
    EXPECT_EQ(core.Profile().flowControl, 6);
    EXPECT_EQ(core.Profile().resultCodeSet, 2);
    EXPECT_EQ(core.Profile().speedNegotiation, 0);
}

TEST(NewSettingCommands_WithoutDigit_StoreZero) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("AT&C&D&KXN"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::OK);
    EXPECT_EQ(core.Profile().dcdMode, 0);
    EXPECT_EQ(core.Profile().dtrMode, 0);
    EXPECT_EQ(core.Profile().flowControl, 0);
    EXPECT_EQ(core.Profile().resultCodeSet, 0);
    EXPECT_EQ(core.Profile().speedNegotiation, 0);
}

TEST(NewSettingCommands_OutOfRange_ReturnErrorAndChangeNothing) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("ATE0&K1"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::Error);
    EXPECT_TRUE(core.Profile().echoEnabled); // E0 earlier on the line did not run
    EXPECT_EQ(core.Profile().flowControl, 3);
}

TEST(FactoryDefaultsAndReset_RestoreNewSettings) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    // Factory profile: &C1 &D2 &K3 X4 N1.
    EXPECT_EQ(core.Profile().dcdMode, 1);
    EXPECT_EQ(core.Profile().dtrMode, 2);
    EXPECT_EQ(core.Profile().flowControl, 3);
    EXPECT_EQ(core.Profile().resultCodeSet, 4);
    EXPECT_EQ(core.Profile().speedNegotiation, 1);

    for (const char* restore : {"AT&F", "ATZ"}) {
        core.Execute(AtCommandParser::Parse("AT&C0&D0&K0X0N0"));
        EXPECT_TRUE(core.Execute(AtCommandParser::Parse(restore)).finalResult == ResultCode::OK);
        EXPECT_EQ(core.Profile().dcdMode, 1);
        EXPECT_EQ(core.Profile().dtrMode, 2);
        EXPECT_EQ(core.Profile().flowControl, 3);
        EXPECT_EQ(core.Profile().resultCodeSet, 4);
        EXPECT_EQ(core.Profile().speedNegotiation, 1);
    }
}

TEST(FClassTest_ReportsDataOnlySupport) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("AT+FCLASS=?"));
    EXPECT_EQ(outcome.infoLines.size(), static_cast<size_t>(1));
    EXPECT_EQ(outcome.infoLines[0], std::string("0"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::OK);
}

TEST(FClassSetToZero_Succeeds) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("AT+FCLASS=0"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::OK);
}

TEST(FClassSetToNonZero_FailsHonestly) {
    // We don't support fax/voice classes - this must not silently succeed.
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("AT+FCLASS=1"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::Error);
}

TEST(GMM_ReturnsIdentificationString) {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto outcome = core.Execute(AtCommandParser::Parse("AT+GMM"));
    EXPECT_EQ(outcome.infoLines.size(), static_cast<size_t>(1));
    EXPECT_EQ(outcome.infoLines[0], std::string("VoIPModem Test Modem"));
    EXPECT_TRUE(outcome.finalResult == ResultCode::OK);
}

TEST(UnsupportedExtendedCommand_HonestlyReturnsError) {
    // +GCI (country of installation) and #CLS (vendor voice/fax class) are
    // real things some modems answer, but we don't model either - make sure
    // we say so rather than guessing a plausible-looking value.
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger(logger, "Test");
    ModemStateMachine core(controller, taggedLogger, "VoIPModem Test Modem");

    const auto gci = core.Execute(AtCommandParser::Parse("AT+GCI?"));
    EXPECT_TRUE(gci.finalResult == ResultCode::Error);

    const auto cls = core.Execute(AtCommandParser::Parse("AT#CLS=?"));
    EXPECT_TRUE(cls.finalResult == ResultCode::Error);
}
