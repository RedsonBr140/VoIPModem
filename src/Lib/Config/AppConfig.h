#pragma once

#include <string>

#include "IniConfig.h"
#include "../Logging/Logger.h"

namespace voipmodem {

// Phase 1 configuration surface only: serial port + modem identity +
// logging. SIP/RTP/DSP settings described in docs/PROJECT_SPEC.md section 5
// are intentionally not read here yet — there is no code that consumes
// them until Phase 2 (SIP) and later phases land. Add them when the
// corresponding module exists, not before.
struct AppConfig {
    std::string comPort = "COM3";
    int baudRate = 115200;
    std::string modemIdentification = "VoIPModem Virtual Modem";
    int autoAnswerRingCount = 0; // S0; 0 = auto-answer disabled
    LogLevel logLevel = LogLevel::Info;
    std::string logFilePath = "voipmodem.log";
};

AppConfig LoadAppConfig(const IniConfig& ini);

} // namespace voipmodem
