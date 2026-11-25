// AT&C / AT&D: carrier (DCD) assertion and the DTE dropping DTR, decided in
// ModemStateMachine and debounced by DtrDebouncer. ModemInstance only moves
// these to and from the serial port; see docs/ARCHITECTURE.md "Serial
// control signals".

#include <chrono>

#include "TestFramework.h"
#include "FakeCallController.h"
#include "../src/Lib/Core/DtrDebouncer.h"
#include "../src/Lib/Core/ModemStateMachine.h"
#include "../src/Lib/Hayes/AtCommandParser.h"
#include "../src/Lib/Logging/Logger.h"
#include "../src/Lib/Logging/TaggedLogger.h"

using voipmodem::AtCommandParser;
using voipmodem::CallEvent;
using voipmodem::CallEventType;
using voipmodem::DtrAction;
using voipmodem::DtrDebouncer;
using voipmodem::Logger;
using voipmodem::ModemState;
using voipmodem::ModemStateMachine;
using voipmodem::ResultCode;
using voipmodem::TaggedLogger;
using voipmodem::test::FakeCallController;

namespace {

struct Rig {
    FakeCallController controller;
    Logger logger;
    TaggedLogger taggedLogger{logger, "Test"};
    ModemStateMachine core{controller, taggedLogger, "VoIPModem Test Modem"};

    void Run(const char* line) { core.Execute(AtCommandParser::Parse(line)); }
    void Event(CallEventType type) { core.HandleCallEvent(CallEvent{type}); }

    // Incoming call answered with ATA: the modem is now training.
    void AnswerIncoming() {
        controller.answerEvent = CallEvent{CallEventType::None};
        Event(CallEventType::IncomingCall);
        Run("ATA");
    }
};

} // namespace

// --- Carrier (AT&C) ---

TEST(Carrier_AmpersandC1_OffAtIdle) {
    Rig r;
    EXPECT_EQ(r.core.Profile().dcdMode, 1);
    EXPECT_TRUE(!r.core.CarrierAsserted());
}

TEST(Carrier_AmpersandC1_OffWhileRingingAndTraining_OnWhenConnected_OffAfterEnd) {
    Rig r;
    r.Event(CallEventType::IncomingCall);
    EXPECT_TRUE(!r.core.CarrierAsserted()); // ringing
    r.controller.answerEvent = CallEvent{CallEventType::None};
    r.Run("ATA");
    EXPECT_TRUE(r.core.CurrentState() == ModemState::Answering);
    EXPECT_TRUE(!r.core.CarrierAsserted()); // training

    r.Event(CallEventType::Connected);
    EXPECT_TRUE(r.core.CurrentState() == ModemState::OnlineData);
    EXPECT_TRUE(r.core.CarrierAsserted());

    r.Event(CallEventType::Ended);
    EXPECT_TRUE(!r.core.CarrierAsserted());
}

TEST(Carrier_AmpersandC1_OffWhileDialing) {
    Rig r;
    r.controller.dialEvent = CallEvent{CallEventType::None};
    r.Run("ATDT5551234");
    EXPECT_TRUE(r.core.CurrentState() == ModemState::Dialing);
    EXPECT_TRUE(!r.core.CarrierAsserted());
}

TEST(Carrier_StaysOnInOnlineCommandMode) {
    Rig r;
    r.Event(CallEventType::Connected);
    r.core.EnterCommandModeFromOnlineData(); // "+++"
    EXPECT_TRUE(r.core.CurrentState() == ModemState::CommandMode);
    EXPECT_TRUE(r.core.DataLinkUp());
    EXPECT_TRUE(r.core.CarrierAsserted());
}

TEST(Carrier_DropsOnAth) {
    Rig r;
    r.Event(CallEventType::Connected);
    r.core.EnterCommandModeFromOnlineData();
    r.Run("ATH");
    EXPECT_TRUE(!r.core.DataLinkUp());
    EXPECT_TRUE(!r.core.CarrierAsserted());
}

TEST(Carrier_AmpersandC0_AlwaysOn) {
    Rig r;
    r.Run("AT&C0");
    EXPECT_TRUE(r.core.CarrierAsserted()); // idle
    r.Event(CallEventType::IncomingCall);
    EXPECT_TRUE(r.core.CarrierAsserted()); // ringing
    r.Event(CallEventType::Connected);
    EXPECT_TRUE(r.core.CarrierAsserted());
    r.Event(CallEventType::Ended);
    EXPECT_TRUE(r.core.CarrierAsserted());
}

TEST(Carrier_FactoryDefaultsAndReset_RestoreAmpersandC1) {
    for (const char* restore : {"AT&F", "ATZ"}) {
        Rig r;
        r.Run("AT&C0");
        EXPECT_TRUE(r.core.CarrierAsserted());
        r.Run(restore);
        EXPECT_EQ(r.core.Profile().dcdMode, 1);
        EXPECT_EQ(r.core.Profile().dtrMode, 2);
        EXPECT_TRUE(!r.core.CarrierAsserted());
    }
}

// --- DTR drop (AT&D) ---

TEST(DtrDrop_AmpersandD2_WhileAnswering_HangsUp) {
    Rig r;
    r.AnswerIncoming();
    const auto outcome = r.core.HandlePeerDtrChange(false);
    EXPECT_TRUE(outcome.action == DtrAction::HungUp);
    EXPECT_TRUE(!outcome.response.hasResult); // NO CARRIER comes with the controller's Ended event
    EXPECT_EQ(r.controller.hangUpCount, 1);
    EXPECT_TRUE(r.core.CurrentState() == ModemState::CommandMode);
}

TEST(DtrDrop_AmpersandD2_WhileDialing_HangsUp) {
    Rig r;
    r.controller.dialEvent = CallEvent{CallEventType::None};
    r.Run("ATDT5551234");
    const auto outcome = r.core.HandlePeerDtrChange(false);
    EXPECT_TRUE(outcome.action == DtrAction::HungUp);
    EXPECT_EQ(r.controller.hangUpCount, 1);
    EXPECT_TRUE(r.core.CurrentState() == ModemState::CommandMode);
}

TEST(DtrDrop_AmpersandD2_WhileOnline_HangsUpAndDropsCarrier) {
    Rig r;
    r.AnswerIncoming();
    r.Event(CallEventType::Connected);
    EXPECT_TRUE(r.core.CarrierAsserted());

    const auto outcome = r.core.HandlePeerDtrChange(false);
    EXPECT_TRUE(outcome.action == DtrAction::HungUp);
    EXPECT_EQ(r.controller.hangUpCount, 1);
    EXPECT_TRUE(r.core.CurrentState() == ModemState::CommandMode);
    EXPECT_TRUE(!r.core.DataLinkUp());
    EXPECT_TRUE(!r.core.CarrierAsserted());
}

TEST(DtrDrop_AmpersandD2_InOnlineCommandMode_HangsUp) {
    Rig r;
    r.Event(CallEventType::Connected);
    r.core.EnterCommandModeFromOnlineData();
    const auto outcome = r.core.HandlePeerDtrChange(false);
    EXPECT_TRUE(outcome.action == DtrAction::HungUp);
    EXPECT_EQ(r.controller.hangUpCount, 1);
    EXPECT_TRUE(!r.core.CarrierAsserted());
}

TEST(DtrDrop_AmpersandD2_AtIdle_DoesNothing) {
    Rig r;
    const auto outcome = r.core.HandlePeerDtrChange(false);
    EXPECT_TRUE(outcome.action == DtrAction::None);
    EXPECT_EQ(r.controller.hangUpCount, 0);
}

TEST(DtrDrop_AmpersandD2_WhileOnlyRinging_LeavesCallRinging) {
    Rig r;
    r.Event(CallEventType::IncomingCall);
    const auto outcome = r.core.HandlePeerDtrChange(false);
    EXPECT_TRUE(outcome.action == DtrAction::None);
    EXPECT_EQ(r.controller.hangUpCount, 0);
    EXPECT_TRUE(r.core.HasIncomingCall());
    EXPECT_TRUE(!r.core.AutoAnswerAllowed());
}

TEST(DtrDrop_AmpersandD0_DoesNothing) {
    Rig r;
    r.Run("AT&D0");
    r.AnswerIncoming();
    r.Event(CallEventType::Connected);
    const auto outcome = r.core.HandlePeerDtrChange(false);
    EXPECT_TRUE(outcome.action == DtrAction::None);
    EXPECT_TRUE(!outcome.response.hasResult);
    EXPECT_EQ(r.controller.hangUpCount, 0);
    EXPECT_TRUE(r.core.CurrentState() == ModemState::OnlineData);
    EXPECT_TRUE(r.core.CarrierAsserted());
    EXPECT_TRUE(r.core.AutoAnswerAllowed());
}

TEST(DtrDrop_AmpersandD1_WhileOnline_EntersOnlineCommandModeWithOk) {
    Rig r;
    r.Run("AT&D1");
    r.Event(CallEventType::Connected);
    const auto outcome = r.core.HandlePeerDtrChange(false);
    EXPECT_TRUE(outcome.action == DtrAction::OnlineCommandMode);
    EXPECT_TRUE(outcome.response.hasResult);
    EXPECT_TRUE(outcome.response.finalResult == ResultCode::OK);
    EXPECT_EQ(r.controller.hangUpCount, 0);
    EXPECT_TRUE(r.core.CurrentState() == ModemState::CommandMode);
    EXPECT_TRUE(r.core.DataLinkUp());
    EXPECT_TRUE(r.core.CarrierAsserted());
}

TEST(DtrDrop_AmpersandD1_WhileTraining_DoesNothing) {
    Rig r;
    r.Run("AT&D1");
    r.AnswerIncoming();
    const auto outcome = r.core.HandlePeerDtrChange(false);
    EXPECT_TRUE(outcome.action == DtrAction::None);
    EXPECT_EQ(r.controller.hangUpCount, 0);
    EXPECT_TRUE(r.core.CurrentState() == ModemState::Answering);
}

TEST(DtrDrop_AmpersandD3_WhileOnline_HangsUpAndResets) {
    Rig r;
    r.Run("AT&D3E0S0=2");
    r.Event(CallEventType::Connected);
    const auto outcome = r.core.HandlePeerDtrChange(false);
    EXPECT_TRUE(outcome.action == DtrAction::HungUpAndReset);
    EXPECT_EQ(r.controller.hangUpCount, 1);
    EXPECT_TRUE(r.core.CurrentState() == ModemState::CommandMode);
    EXPECT_TRUE(!r.core.CarrierAsserted());
    EXPECT_TRUE(r.core.Profile().echoEnabled);           // reset as ATZ
    EXPECT_EQ(r.core.Profile().dtrMode, 2);
    EXPECT_EQ(static_cast<int>(r.core.Registers().Get(0)), 0);
}

TEST(DtrDrop_AmpersandD3_AtIdle_ResetsWithoutHangingUp) {
    Rig r;
    r.Run("AT&D3E0");
    const auto outcome = r.core.HandlePeerDtrChange(false);
    EXPECT_TRUE(outcome.action == DtrAction::HungUpAndReset);
    EXPECT_EQ(r.controller.hangUpCount, 0);
    EXPECT_TRUE(r.core.Profile().echoEnabled);
}

// --- Auto-answer while DTR is low ---

TEST(AutoAnswer_AllowedWhileDtrHigh) {
    Rig r;
    EXPECT_TRUE(r.core.DteReady());
    EXPECT_TRUE(r.core.AutoAnswerAllowed());
}

TEST(AutoAnswer_AmpersandD2_NotAllowedWhileDtrLow_AllowedAgainWhenRaised) {
    Rig r;
    r.core.InitializeAutoAnswerRegister(1);
    r.core.HandlePeerDtrChange(false);
    EXPECT_TRUE(!r.core.AutoAnswerAllowed());
    r.core.HandlePeerDtrChange(true);
    EXPECT_TRUE(r.core.AutoAnswerAllowed());
}

TEST(AutoAnswer_SetDteReadyAtOpen_DoesNotActOrReset) {
    Rig r;
    r.core.InitializeAutoAnswerRegister(2);
    r.Run("AT&D3");
    r.core.SetDteReady(false);
    EXPECT_TRUE(!r.core.AutoAnswerAllowed());
    EXPECT_EQ(static_cast<int>(r.core.Registers().Get(0)), 2); // no ATZ-style reset
    EXPECT_EQ(r.core.Profile().dtrMode, 3);
}

TEST(AutoAnswer_AmpersandD0_AllowedWhileDtrLow) {
    Rig r;
    r.Run("AT&D0");
    r.core.HandlePeerDtrChange(false);
    EXPECT_TRUE(r.core.AutoAnswerAllowed());
}

// --- DtrDebouncer ---

namespace {
using Ms = std::chrono::milliseconds;
const DtrDebouncer::Clock::time_point kT0{};
} // namespace

TEST(DtrDebouncer_DropReportedOnlyAfterDelay) {
    DtrDebouncer d(Ms(50));
    d.Reset(true);
    EXPECT_TRUE(d.Update(false, kT0) == DtrDebouncer::Change::None);
    EXPECT_TRUE(d.Update(false, kT0 + Ms(30)) == DtrDebouncer::Change::None);
    EXPECT_TRUE(d.Level());
    EXPECT_TRUE(d.Update(false, kT0 + Ms(50)) == DtrDebouncer::Change::Dropped);
    EXPECT_TRUE(!d.Level());
    EXPECT_TRUE(d.Update(false, kT0 + Ms(500)) == DtrDebouncer::Change::None); // reported once
}

TEST(DtrDebouncer_GlitchShorterThanDelay_Ignored) {
    DtrDebouncer d(Ms(50));
    d.Reset(true);
    EXPECT_TRUE(d.Update(false, kT0) == DtrDebouncer::Change::None);
    EXPECT_TRUE(d.Update(true, kT0 + Ms(20)) == DtrDebouncer::Change::None);
    // A fresh low starts its own timer.
    EXPECT_TRUE(d.Update(false, kT0 + Ms(60)) == DtrDebouncer::Change::None);
    EXPECT_TRUE(d.Update(false, kT0 + Ms(100)) == DtrDebouncer::Change::None);
    EXPECT_TRUE(d.Level());
}

TEST(DtrDebouncer_SingleLowSample_NeverDrops) {
    // The run loop may sample only every ~200 ms: one low sample, however
    // far from the last high one, is not yet a drop.
    DtrDebouncer d(Ms(50));
    d.Reset(true);
    EXPECT_TRUE(d.Update(false, kT0 + Ms(1000)) == DtrDebouncer::Change::None);
    EXPECT_TRUE(d.Update(true, kT0 + Ms(1200)) == DtrDebouncer::Change::None);
    EXPECT_TRUE(d.Level());
}

TEST(DtrDebouncer_RiseReportedAtOnce) {
    DtrDebouncer d(Ms(50));
    d.Reset(false);
    EXPECT_TRUE(d.Update(false, kT0) == DtrDebouncer::Change::None);
    EXPECT_TRUE(d.Update(true, kT0 + Ms(1)) == DtrDebouncer::Change::Raised);
    EXPECT_TRUE(d.Update(true, kT0 + Ms(2)) == DtrDebouncer::Change::None);
}
