#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace voipmodem {

enum class LogLevel {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3
};

// Minimal leveled logger. Writes to an optional log file and echoes
// Warn/Error to stderr. Callers are responsible for never passing
// credentials (SIP passwords, auth tokens) into logged messages.
class Logger {
public:
    void Init(LogLevel level, const std::string& logFilePath);

    void Log(LogLevel level, const std::string& message);

    void Error(const std::string& message) { Log(LogLevel::Error, message); }
    void Warn(const std::string& message) { Log(LogLevel::Warn, message); }
    void Info(const std::string& message) { Log(LogLevel::Info, message); }
    void Debug(const std::string& message) { Log(LogLevel::Debug, message); }

    static const char* ToString(LogLevel level);

private:
    LogLevel level_ = LogLevel::Info;
    std::ofstream file_;
    std::mutex mutex_;
};

} // namespace voipmodem
