#include "AppConfig.h"

#include <algorithm>
#include <cctype>

namespace voipmodem {

namespace {

LogLevel ParseLogLevel(const std::string& value, LogLevel defaultValue) {
    std::string upper = value;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (upper == "ERROR") return LogLevel::Error;
    if (upper == "WARN") return LogLevel::Warn;
    if (upper == "INFO") return LogLevel::Info;
    if (upper == "DEBUG") return LogLevel::Debug;
    return defaultValue;
}

} // namespace

AppConfig LoadAppConfig(const IniConfig& ini) {
    AppConfig config;
    config.comPort = ini.GetString("Modem", "ComPort", config.comPort);
    config.baudRate = ini.GetInt("Modem", "BaudRate", config.baudRate);
    config.modemIdentification = ini.GetString("Modem", "Identification", config.modemIdentification);
    config.autoAnswerRingCount = ini.GetInt("Modem", "AutoAnswerRingCount", config.autoAnswerRingCount);
    config.logLevel = ParseLogLevel(ini.GetString("Logging", "Level", "Info"), config.logLevel);
    config.logFilePath = ini.GetString("Logging", "File", config.logFilePath);
    return config;
}

} // namespace voipmodem
