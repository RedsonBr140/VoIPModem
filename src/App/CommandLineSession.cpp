#include "CommandLineSession.h"

#include "../Lib/Hayes/AtCommandParser.h"

namespace voipmodem {

namespace {
constexpr uint8_t kCR = 0x0D;
constexpr uint8_t kLF = 0x0A;
constexpr uint8_t kBackspace = 0x08;
constexpr uint8_t kDelete = 0x7F;

// Response framing is "\r\n<text>\r\n" for verbose text and info lines,
// and a bare numeric code followed by "\r\n" in numeric mode. A real
// Hayes modem in V0 mode terminates with a lone \r; we use \r\n uniformly
// instead for broader terminal-emulator compatibility. This is a known,
// documented simplification (see docs/ARCHITECTURE.md).
std::string FrameTextLine(const std::string& text) {
    return "\r\n" + text + "\r\n";
}
} // namespace

CommandLineSession::CommandLineSession(ISerialPort& port, ModemStateMachine& core, TaggedLogger& logger)
    : port_(port), core_(core), logger_(logger) {}

void CommandLineSession::Echo(uint8_t byte) {
    if (core_.Profile().echoEnabled) {
        port_.Write(std::vector<uint8_t>{byte});
    }
}

void CommandLineSession::OnByteReceived(uint8_t byte) {
    if (byte == kCR) {
        Echo(byte);
        const std::string line = lineBuffer_;
        lineBuffer_.clear();
        ProcessLine(line);
        return;
    }

    if (byte == kLF) {
        // Many terminals send CR then LF; CR alone already terminated the
        // line above, so a bare LF is not echoed or buffered.
        return;
    }

    if (byte == kBackspace || byte == kDelete) {
        if (!lineBuffer_.empty()) {
            lineBuffer_.pop_back();
            if (core_.Profile().echoEnabled) {
                port_.Write(std::string("\b \b"));
            }
        }
        return;
    }

    if (lineBuffer_.size() >= kMaxLineLength) {
        return; // silently drop overflow rather than growing unbounded
    }

    lineBuffer_.push_back(static_cast<char>(byte));
    Echo(byte);
}

void CommandLineSession::ProcessLine(const std::string& line) {
    logger_.Debug("Command line: " + line);
    const AtParseResult parsed = AtCommandParser::Parse(line);
    const ExecutionOutcome outcome = core_.Execute(parsed);
    SendResponse(outcome);
}

void CommandLineSession::SendResponse(const ExecutionOutcome& outcome) {
    if (!outcome.hasResult) {
        return;
    }

    for (const std::string& infoLine : outcome.infoLines) {
        port_.Write(FrameTextLine(infoLine));
    }

    if (outcome.suppressFinalResult) {
        return;
    }

    if (core_.Profile().verboseResultCodes) {
        port_.Write(FrameTextLine(ToVerboseText(outcome.finalResult)));
    } else {
        port_.Write(std::to_string(ToNumericCode(outcome.finalResult)) + "\r\n");
    }
}

void CommandLineSession::SendUnsolicited(const ExecutionOutcome& outcome) {
    SendResponse(outcome);
}

void CommandLineSession::SendUnsolicitedRing() {
    ExecutionOutcome outcome;
    outcome.finalResult = ResultCode::Ring;
    outcome.suppressFinalResult = core_.Profile().quietMode;
    SendResponse(outcome);
}

void CommandLineSession::TriggerAutoAnswer() {
    ProcessLine("ATA");
}

} // namespace voipmodem
