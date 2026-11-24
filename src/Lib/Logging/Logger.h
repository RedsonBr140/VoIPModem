#pragma once

#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace voipmodem {

enum class LogLevel {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3
};

// One line kept in memory for the control API's event log (see
// Logger::EntriesSince). `seq` increases by one per line, from 1.
struct LogEntry {
    std::uint64_t seq = 0;
    std::string time;    // "YYYY-MM-DD HH:MM:SS", local time
    LogLevel level = LogLevel::Info;
    std::string message;
};

// Leveled logger, shared by every thread. Writes to an optional log file
// (rotated by size if asked), echoes to stdout/stderr when there is a
// console, and keeps the last lines in memory for the control API.
// Callers are responsible for never passing credentials (SIP passwords,
// auth tokens) into logged messages.
class Logger {
public:
    // `rotateBytes` 0 = never rotate; otherwise, once the file reaches it,
    // file -> file.1 -> ... -> file.<keepFiles> (the oldest is deleted).
    void Init(LogLevel level, const std::string& logFilePath, std::uint64_t rotateBytes = 0, int keepFiles = 5);

    // Applies a new level / file / rotation while running.
    void Reconfigure(LogLevel level, const std::string& logFilePath, std::uint64_t rotateBytes, int keepFiles);

    void SetConsoleEcho(bool enabled);

    void Log(LogLevel level, const std::string& message);

    void Error(const std::string& message) { Log(LogLevel::Error, message); }
    void Warn(const std::string& message) { Log(LogLevel::Warn, message); }
    void Info(const std::string& message) { Log(LogLevel::Info, message); }
    void Debug(const std::string& message) { Log(LogLevel::Debug, message); }

    LogLevel Level() const;
    std::string FilePath() const;

    // Lines with seq > `afterSeq`, oldest first, at most `max` (the newest
    // ones if more are kept). The memory log holds the last kMemoryLines.
    std::vector<LogEntry> EntriesSince(std::uint64_t afterSeq, size_t max) const;
    std::uint64_t LastSeq() const;

    static const char* ToString(LogLevel level);

    static constexpr size_t kMemoryLines = 2000;

private:
    void OpenFileLocked();
    void RotateLocked();

    LogLevel level_ = LogLevel::Info;
    std::string path_;
    std::uint64_t rotateBytes_ = 0;
    int keepFiles_ = 5;
    std::uint64_t fileBytes_ = 0;
    bool consoleEcho_ = true;
    std::ofstream file_;
    std::deque<LogEntry> memory_;
    std::uint64_t nextSeq_ = 1;
    mutable std::mutex mutex_;
};

} // namespace voipmodem
