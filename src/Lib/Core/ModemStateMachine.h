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
    bool suppressFinalResult = false;
    bool hasResult = true;
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

    ModemState CurrentState() const { return state_; }
    const ModemProfile& Profile() const { return profile_; }
    const SRegisters& Registers() const { return registers_; }

private:
    ExecutionOutcome ExecuteExtendedCommand(const AtCommand& cmd) const;

    ICallController& callController_;
    TaggedLogger& logger_;
    std::string identification_;
    ModemState state_ = ModemState::CommandMode;
    ModemProfile profile_;
    SRegisters registers_;
    bool hasIncomingCall_ = false;
};

} // namespace voipmodem
