#pragma once

#include <cstdint>
#include <string>

#include "../Lib/Core/ModemStateMachine.h"
#include "../Lib/Serial/ISerialPort.h"
#include "../Lib/Logging/TaggedLogger.h"

namespace voipmodem {

// Bridges raw serial bytes and the command-mode AT parser/state machine:
// buffers a line, performs local echo (ATE), and frames responses
// according to ATV/ATQ. Online data mode (raw passthrough once a modem
// connection exists) is out of scope until Phase 2+ gives this something
// real to carry.
class CommandLineSession {
public:
    CommandLineSession(ISerialPort& port, ModemStateMachine& core, TaggedLogger& logger);

    // Feed one received byte. Call this from the serial read loop.
    void OnByteReceived(uint8_t byte);

    // Sends a response that wasn't triggered by a command the DTE just
    // typed — e.g. the eventual CONNECT/NO CARRIER/etc. from an async
    // ICallController event (see ModemStateMachine::HandleCallEvent) or an
    // unsolicited RING. Applies the same ATV/ATQ framing rules as a normal
    // command response.
    void SendUnsolicited(const ExecutionOutcome& outcome);

    // Convenience for the unsolicited RING line specifically (no info
    // lines, just the RING result code, framed per ATV/ATQ as usual).
    void SendUnsolicitedRing();

    // Runs "ATA" through the normal command path, exactly as if the DTE
    // had typed it — used for S0-register auto-answer. Keeps all the
    // actual answer logic in one place (ModemStateMachine/ICallController)
    // instead of duplicating it for the auto-answer case.
    void TriggerAutoAnswer();

private:
    void ProcessLine(const std::string& line);
    void SendResponse(const ExecutionOutcome& outcome);
    void Echo(uint8_t byte);

    ISerialPort& port_;
    ModemStateMachine& core_;
    TaggedLogger& logger_;
    std::string lineBuffer_;

    static constexpr size_t kMaxLineLength = 256;
};

} // namespace voipmodem
