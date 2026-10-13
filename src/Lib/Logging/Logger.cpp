#include "Logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace voipmodem {

namespace {

std::string TimestampNow() {
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);
    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

} // namespace

void Logger::Init(LogLevel level, const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
    if (!logFilePath.empty()) {
        file_.open(logFilePath, std::ios::app);
    }
}

void Logger::Log(LogLevel level, const std::string& message) {
    if (level > level_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream line;
    line << "[" << TimestampNow() << "] [" << ToString(level) << "] " << message;

    if (file_.is_open()) {
        file_ << line.str() << std::endl;
    }

    if (level == LogLevel::Error || level == LogLevel::Warn) {
        std::cerr << line.str() << std::endl;
    } else {
        std::cout << line.str() << std::endl;
    }
}

const char* Logger::ToString(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return "ERROR";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Debug: return "DEBUG";
    }
    return "UNKNOWN";
}

} // namespace voipmodem
