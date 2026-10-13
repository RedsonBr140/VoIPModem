#pragma once

#include <string>
#include <vector>

#include "ICallController.h"
#include "ModemProfile.h"
#include "ModemState.h"
#include "ResultCodes.h"
#include "SRegisters.h"
#include "../Hayes/AtCommand.h"
#include "../Logging/Logger.h"

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
class ModemStateMachine {
public:
    ModemStateMachine(ICallController& callController, Logger& logger, std::string identification);

    ExecutionOutcome Execute(const AtParseResult& parsed);

    ModemState CurrentState() const { return state_; }
    const ModemProfile& Profile() const { return profile_; }
    const SRegisters& Registers() const { return registers_; }

private:
    static ResultCode ToResultCode(CallOutcome outcome);

    ICallController& callController_;
    Logger& logger_;
    std::string identification_;
    ModemState state_ = ModemState::CommandMode;
    ModemProfile profile_;
    SRegisters registers_;
};

} // namespace voipmodem
