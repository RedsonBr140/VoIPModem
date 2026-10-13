#pragma once

#include <string>
#include <vector>

#include "IniConfig.h"
#include "../Logging/Logger.h"

namespace voipmodem {

// Phase 1 configuration surface only: serial port + modem identity +
// logging. SIP/RTP/DSP settings described in docs/PROJECT_SPEC.md section 5
// are intentionally not read here yet — there is no code that consumes
// them until Phase 2 (SIP) and later phases land. Add them when the
// corresponding module exists, not before.
struct ModemInstanceConfig {
    // From "[Modem.<name>]"; empty for a legacy unnamed "[Modem]" section.
    // Used only to label log lines when multiple instances run at once —
    // see TaggedLogger.
    std::string name;
    std::string comPort = "COM3";
    int baudRate = 115200;
    std::string modemIdentification = "VoIPModem Virtual Modem";
    int autoAnswerRingCount = 0; // S0; 0 = auto-answer disabled
};

struct AppConfig {
    LogLevel logLevel = LogLevel::Info;
    std::string logFilePath = "voipmodem.log";

    // One entry per modem line to run. Always has at least one entry —
    // LoadAppConfig() falls back to a single default instance if the INI
    // defines none, matching pre-multi-instance Phase 1 behavior.
    std::vector<ModemInstanceConfig> modems;
};

AppConfig LoadAppConfig(const IniConfig& ini);

} // namespace voipmodem
