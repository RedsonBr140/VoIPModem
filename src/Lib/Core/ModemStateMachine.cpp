#include "ModemStateMachine.h"

#include <iomanip>
#include <sstream>

namespace voipmodem {

namespace {

std::string FormatRegisterValue(int value) {
    std::ostringstream oss;
    oss << std::setw(3) << std::setfill('0') << value;
    return oss.str();
}

} // namespace

ModemStateMachine::ModemStateMachine(ICallController& callController, TaggedLogger& logger,
                                      std::string identification)
    : callController_(callController), logger_(logger), identification_(std::move(identification)) {}

ResultCode ModemStateMachine::ToResultCode(CallOutcome outcome) {
    switch (outcome) {
        case CallOutcome::NoCarrier: return ResultCode::NoCarrier;
        case CallOutcome::NoAnswer: return ResultCode::NoAnswer;
        case CallOutcome::Busy: return ResultCode::Busy;
        case CallOutcome::NoDialtone: return ResultCode::NoDialtone;
        case CallOutcome::Ringing: return ResultCode::NoCarrier; // not reachable in Phase 1
    }
    return ResultCode::Error;
}

ExecutionOutcome ModemStateMachine::Execute(const AtParseResult& parsed) {
    ExecutionOutcome outcome;
    outcome.suppressFinalResult = profile_.quietMode;

    if (!parsed.isValid) {
        outcome.finalResult = ResultCode::Error;
        return outcome;
    }

    if (parsed.commands.empty()) {
        outcome.hasResult = false; // blank line: real modems stay silent
        return outcome;
    }

    for (const AtCommand& cmd : parsed.commands) {
        switch (cmd.type) {
            case AtCommandType::Attention:
                break;

            case AtCommandType::Identify:
                outcome.infoLines.push_back(identification_);
                break;

            case AtCommandType::Reset:
                profile_.ResetToFactoryDefaults();
                registers_.ResetToFactoryDefaults();
                state_ = ModemState::CommandMode;
                break;

            case AtCommandType::SetEcho:
                profile_.echoEnabled = cmd.numericParam.value_or(0) != 0;
                break;

            case AtCommandType::SetVerbose:
                profile_.verboseResultCodes = cmd.numericParam.value_or(0) != 0;
                break;

            case AtCommandType::SetQuiet:
                profile_.quietMode = cmd.numericParam.value_or(0) != 0;
                outcome.suppressFinalResult = profile_.quietMode;
                break;

            case AtCommandType::SetRegister:
                registers_.Set(cmd.registerIndex.value_or(0),
                                static_cast<uint8_t>(cmd.numericParam.value_or(0)));
                break;

            case AtCommandType::QueryRegister:
                outcome.infoLines.push_back(FormatRegisterValue(registers_.Get(cmd.registerIndex.value_or(0))));
                break;

            case AtCommandType::FactoryDefaults:
                profile_.ResetToFactoryDefaults();
                registers_.ResetToFactoryDefaults();
                break;

            case AtCommandType::Dial: {
                state_ = ModemState::Dialing;
                logger_.Info("ATD" + std::string(cmd.dialMode == AtDialMode::Tone ? "T" :
                                                   cmd.dialMode == AtDialMode::Pulse ? "P" : "") +
                             " dialing '" + cmd.stringParam + "'");
                const CallOutcome result = callController_.Dial(cmd.stringParam);
                state_ = ModemState::CommandMode; // no backend yet connects/stays online
                outcome.finalResult = ToResultCode(result);
                return outcome; // parser guarantees Dial is the last command in the chain
            }

            case AtCommandType::Answer: {
                state_ = ModemState::Answering;
                logger_.Info("ATA answering incoming call");
                const CallOutcome result = callController_.Answer();
                state_ = ModemState::CommandMode;
                outcome.finalResult = ToResultCode(result);
                return outcome;
            }

            case AtCommandType::HangUp:
                logger_.Info("ATH hanging up");
                callController_.HangUp();
                state_ = ModemState::CommandMode;
                break;

            case AtCommandType::Unknown:
            default:
                outcome.finalResult = ResultCode::Error;
                return outcome;
        }
    }

    outcome.finalResult = ResultCode::OK;
    return outcome;
}

} // namespace voipmodem
