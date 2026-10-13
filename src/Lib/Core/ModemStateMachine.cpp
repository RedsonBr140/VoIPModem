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
                callController_.Dial(cmd.stringParam);
                // No immediate response: a real modem doesn't print CONNECT/NO
                // CARRIER/etc. until the call actually resolves, which happens
                // later via HandleCallEvent(). Parser guarantees Dial is the
                // last command in its chain.
                outcome.hasResult = false;
                return outcome;
            }

            case AtCommandType::Answer: {
                state_ = ModemState::Answering;
                logger_.Info("ATA answering incoming call");
                callController_.Answer();
                hasIncomingCall_ = false; // acted on it; stop any RING cadence/auto-answer countdown
                outcome.hasResult = false; // result (CONNECT/NO CARRIER) arrives via HandleCallEvent()
                return outcome;
            }

            case AtCommandType::HangUp:
                logger_.Info("ATH hanging up");
                callController_.HangUp();
                hasIncomingCall_ = false;
                state_ = ModemState::CommandMode;
                break;

            case AtCommandType::ExtendedCommand:
                return ExecuteExtendedCommand(cmd); // always the last command in its line

            case AtCommandType::Unknown:
            default:
                outcome.finalResult = ResultCode::Error;
                return outcome;
        }
    }

    outcome.finalResult = ResultCode::OK;
    return outcome;
}

ExecutionOutcome ModemStateMachine::ExecuteExtendedCommand(const AtCommand& cmd) const {
    ExecutionOutcome outcome;
    outcome.suppressFinalResult = profile_.quietMode;

    const std::string key = std::string(1, cmd.extendedPrefix) + cmd.extendedName;

    // Only commands we have a real, true answer for. Everything else
    // (+GCI country-of-installation, #CLS vendor voice/fax class, ...)
    // honestly returns ERROR rather than guessing a plausible-looking
    // value — see docs/ARCHITECTURE.md "V.25ter extended commands".
    if (key == "+FCLASS") {
        // We are a plain data modem: no fax (class 1/2) or voice support.
        switch (cmd.extendedSuffix) {
            case AtExtendedSuffix::Test:
                outcome.infoLines.push_back("0");
                break;
            case AtExtendedSuffix::Query:
                outcome.infoLines.push_back("0");
                break;
            case AtExtendedSuffix::SetValue:
                if (cmd.extendedValue != "0") {
                    outcome.finalResult = ResultCode::Error; // asked for a class we don't support
                    return outcome;
                }
                break;
            case AtExtendedSuffix::None:
                break;
        }
        outcome.finalResult = ResultCode::OK;
        return outcome;
    }

    if (key == "+GMI") {
        outcome.infoLines.push_back("VoIPModem Project");
        outcome.finalResult = ResultCode::OK;
        return outcome;
    }

    if (key == "+GMM") {
        outcome.infoLines.push_back(identification_);
        outcome.finalResult = ResultCode::OK;
        return outcome;
    }

    if (key == "+GMR") {
        outcome.infoLines.push_back("Phase 1");
        outcome.finalResult = ResultCode::OK;
        return outcome;
    }

    outcome.finalResult = ResultCode::Error;
    return outcome;
}

ExecutionOutcome ModemStateMachine::HandleCallEvent(const CallEvent& event) {
    ExecutionOutcome outcome;
    outcome.suppressFinalResult = profile_.quietMode;

    switch (event.type) {
        case CallEventType::None:
            outcome.hasResult = false;
            return outcome;

        case CallEventType::IncomingCall:
            // Not a response by itself — ModemInstance owns the RING cadence
            // and S0 auto-answer countdown based on HasIncomingCall().
            hasIncomingCall_ = true;
            state_ = ModemState::Answering;
            outcome.hasResult = false;
            return outcome;

        case CallEventType::Connected:
            hasIncomingCall_ = false;
            state_ = ModemState::OnlineData;
            outcome.finalResult = ResultCode::Connect;
            return outcome;

        case CallEventType::Ended:
            hasIncomingCall_ = false;
            state_ = ModemState::CommandMode;
            outcome.finalResult = event.result;
            return outcome;
    }

    outcome.hasResult = false;
    return outcome;
}

} // namespace voipmodem
