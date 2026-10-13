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
