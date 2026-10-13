#pragma once

#include <memory>
#include <string>

#include "CommandLineSession.h"
#include "../Lib/Config/AppConfig.h"
#include "../Lib/Core/ModemStateMachine.h"
#include "../Lib/Core/NullCallController.h"
#include "../Lib/Logging/Logger.h"
#include "../Lib/Serial/WindowsSerialPort.h"

namespace voipmodem {

// Requests that a running Application::Run() loop stop at the next
// opportunity. Safe to call from a console control handler (Ctrl+C).
void RequestStop();

// Wires together config, logging, the serial port, the Phase 1 stub call
// controller, and the modem core, then runs the blocking read loop that
// drives command-mode AT processing. See docs/ARCHITECTURE.md for how
// this fits the rest of the (currently unbuilt) system.
class Application {
public:
    explicit Application(std::string configPath);

    // Returns a process exit code (0 on clean shutdown).
    int Run();

private:
    std::string configPath_;
    Logger logger_;
    AppConfig config_;
    WindowsSerialPort serialPort_;
    NullCallController callController_;
    ModemStateMachine core_;
    CommandLineSession session_;
};

} // namespace voipmodem
