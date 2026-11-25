#pragma once

#include <string>
#include <vector>

#include "ICallController.h"
#include "ModemProfile.h"
#include "ModemState.h"
#include "ResultCodes.h"
#include "SRegisters.h"
#include "../Hayes/AtCommand.h"
#include "../Logging/TaggedLogger.h"

namespace voipmodem {

// Outcome of executing one parsed command line. `infoLines` holds any
// query/identification text that must be emitted before the terminal
// result code (e.g. ATI's identification string, ATSn?'s register value).
// `hasResult` is false only for a blank input line, which real modems
// simply ignore.
struct ExecutionOutcome {
    std::vector<std::string> infoLines;
    ResultCode finalResult = ResultCode::OK;
    std::string resultSuffix; // verbose mode only, e.g. " 31200" -> "CONNECT 31200"
    bool suppressFinalResult = false;
    bool hasResult = true;
};

// What ModemStateMachine::HandlePeerDtrChange() did about the DTE's DTR
// line, per the AT&D setting. `response` is what to send the DTE, if
// anything (only &D1's OK; a &D2/&D3 hangup is reported by the call
// controller's Ended event as NO CARRIER, as for ATH).
enum class DtrAction {
    None,                // &D0, DTR rising, or nothing to act on
    OnlineCommandMode,   // &D1: left OnlineData, call still up
    HungUp,              // &D2: hung up, back in command mode
    HungUpAndReset       // &D3: hung up if in a call, then as ATZ
};

struct DtrOutcome {
    DtrAction action = DtrAction::None;
    ExecutionOutcome response;
};

// The modem core: owns modem state, the ATE/ATV/ATQ profile, and the
// S-register bank, and executes parsed AT commands against them. It talks
// to telephony only through ICallController, so it has no idea whether
// Dial()/Answer() are backed by a stub (Phase 1) or real SIP (Phase 2+).
//
// Call progress is asynchronous (see ICallController), so Execute() never
// produces a result for ATD/ATA directly — it only starts the attempt
// (hasResult == false) and the caller (ModemInstance) is expected to poll
// the call controller and route whatever it gets through HandleCallEvent()
// to get the eventual, unsolicited RING/CONNECT/NO CARRIER/etc. response.
class ModemStateMachine {
public:
    ModemStateMachine(ICallController& callController, TaggedLogger& logger, std::string identification);

    ExecutionOutcome Execute(const AtParseResult& parsed);

    // Translates one ICallController::PollEvent() result into the
    // response to send (if any) and updates modem state accordingly.
    // `hasResult` is false for IncomingCall — that event only means "start
    // a RING cadence", which is ModemInstance's job, not a direct response.
    ExecutionOutcome HandleCallEvent(const CallEvent& event);

    // True from the moment an IncomingCall event arrives until it is
    // answered (ATA / auto-answer) or the caller gives up. ModemInstance
    // uses this to drive RING cadence and S0 auto-answer counting.
    bool HasIncomingCall() const { return hasIncomingCall_; }

    // Seeds S0 (auto-answer ring count) from config at startup — the
    // config value (AutoAnswerRingCount=) only ever reaches the register
    // this way; it is not re-applied by ATZ/AT&F, which reset S0 to 0
    // like any other hardware factory default, not back to this value.
    void InitializeAutoAnswerRegister(int rings) { registers_.Set(0, static_cast<uint8_t>(rings)); }

    // Drops back to CommandMode from an active OnlineData call without
    // touching the call itself (ICallController is never told) -- the
    // Hayes "+++" escape sequence's actual job. ModemInstance owns the
    // guard-time timing that decides when a real escape has happened;
    // this just performs the state transition once it has. No-op if not
    // currently in OnlineData.
    void EnterCommandModeFromOnlineData();

    // --- Serial control signals (docs/ARCHITECTURE.md "Serial control
    // signals"). ModemInstance reads and drives the port; the decisions
    // are made here.

    // True from a Connected event until the call ends (Ended, ATH, or a
    // DTR hangup), including online command mode after "+++".
    bool DataLinkUp() const { return dataLinkUp_; }

    // Whether DCD should be on: always under &C0, only while the data
    // link is up under &C1.
    bool CarrierAsserted() const { return profile_.dcdMode == 0 || dataLinkUp_; }

    // The DTE's DTR as last reported (debounced) by HandlePeerDtrChange().
    // Starts true, so a line whose DTR is never reported behaves as before.
    bool DteReady() const { return dteReady_; }

    // Records the DTE's DTR level without acting on it: the level found
    // when the port opens is a starting point, not a drop.
    void SetDteReady(bool ready) { dteReady_ = ready; }

    // S0 auto-answer may run: not while DTR is low, unless &D0.
    bool AutoAnswerAllowed() const { return profile_.dtrMode == 0 || dteReady_; }

    // The DTE raised (true) or dropped (false) DTR. A drop is acted on
    // per AT&D only while a call is in progress: dialing, answering after
    // ATA or auto-answer, training, online, or online command mode. An
    // incoming call that is only ringing is left ringing; with DTR low it
    // is not auto-answered (see AutoAnswerAllowed()). &D3 resets as ATZ
    // even with no call.
    DtrOutcome HandlePeerDtrChange(bool asserted);

    ModemState CurrentState() const { return state_; }
    const ModemProfile& Profile() const { return profile_; }
    const SRegisters& Registers() const { return registers_; }

    // The digits/number most recently passed to ATD, with any T/P
    // dial-mode modifier already stripped (same string handed to
    // ICallController::Dial()). Valid once CurrentState() ==
    // ModemState::Dialing; used by ModemInstance to know what to play
    // as a local DTMF dialing sound (see docs/ROADMAP.md Phase 4) --
    // purely cosmetic, not re-sent anywhere.
    const std::string& LastDialedNumber() const { return lastDialedNumber_; }

private:
    ExecutionOutcome ExecuteExtendedCommand(const AtCommand& cmd) const;
    bool CallInProgress() const;
    void HangUpForDtr();

    ICallController& callController_;
    TaggedLogger& logger_;
    std::string identification_;
    ModemState state_ = ModemState::CommandMode;
    ModemProfile profile_;
    SRegisters registers_;
    bool hasIncomingCall_ = false;
    bool dataLinkUp_ = false;
    bool dteReady_ = true;
    std::string lastDialedNumber_;
};

} // namespace voipmodem
