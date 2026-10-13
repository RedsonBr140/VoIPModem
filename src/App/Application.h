#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ModemInstance.h"
#include "../Lib/Config/AppConfig.h"
#include "../Lib/Logging/Logger.h"

namespace voipmodem {

// Requests that a running Application::Run() loop stop at the next
// opportunity. Safe to call from a console control handler (Ctrl+C).
void RequestStop();

// Top-level orchestrator: loads config, initializes the shared logger,
// opens and starts one ModemInstance per configured modem line, then
// waits for a stop request before shutting all instances down cleanly.
// Owns no serial port, call controller, or state machine itself — those
// belong to ModemInstance, one per modem line. See
// docs/ARCHITECTURE.md "Multiple modem instances".
class Application {
public:
    explicit Application(std::string configPath);

    // Returns a process exit code (0 on clean shutdown).
    int Run();

private:
    std::string configPath_;
    Logger logger_;
    AppConfig config_;
    std::vector<std::unique_ptr<ModemInstance>> instances_;
};

} // namespace voipmodem
