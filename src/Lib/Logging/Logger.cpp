#include "Logger.h"

#include <chrono>
#include <cstdio>
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
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

} // namespace

void Logger::Init(LogLevel level, const std::string& logFilePath, std::uint64_t rotateBytes, int keepFiles) {
    Reconfigure(level, logFilePath, rotateBytes, keepFiles);
}

void Logger::Reconfigure(LogLevel level, const std::string& logFilePath, std::uint64_t rotateBytes, int keepFiles) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
    rotateBytes_ = rotateBytes;
    keepFiles_ = keepFiles < 1 ? 1 : keepFiles;
    if (logFilePath != path_ || !file_.is_open()) {
        path_ = logFilePath;
        OpenFileLocked();
    }
}

void Logger::SetConsoleEcho(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    consoleEcho_ = enabled;
}

void Logger::OpenFileLocked() {
    if (file_.is_open()) {
        file_.close();
    }
    fileBytes_ = 0;
    if (path_.empty()) {
        return;
    }
    file_.open(path_, std::ios::app | std::ios::binary);
    if (file_.is_open()) {
        file_.seekp(0, std::ios::end);
        const auto pos = file_.tellp();
        fileBytes_ = pos > 0 ? static_cast<std::uint64_t>(pos) : 0;
    }
}

void Logger::RotateLocked() {
    file_.close();
    // file.<keep> is the oldest; drop it, shift the rest up by one.
    std::remove((path_ + "." + std::to_string(keepFiles_)).c_str());
    for (int i = keepFiles_ - 1; i >= 1; --i) {
        std::rename((path_ + "." + std::to_string(i)).c_str(), (path_ + "." + std::to_string(i + 1)).c_str());
    }
    std::rename(path_.c_str(), (path_ + ".1").c_str());
    OpenFileLocked();
}

void Logger::Log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (level > level_) {
        return;
    }

    LogEntry entry;
    entry.seq = nextSeq_++;
    entry.time = TimestampNow();
    entry.level = level;
    entry.message = message;

    const std::string line = "[" + entry.time + "] [" + ToString(level) + "] " + message;

    if (file_.is_open()) {
        file_ << line << "\r\n";
        file_.flush();
        fileBytes_ += line.size() + 2;
        if (rotateBytes_ > 0 && fileBytes_ >= rotateBytes_) {
            RotateLocked();
        }
    }

    if (consoleEcho_) {
        if (level == LogLevel::Error || level == LogLevel::Warn) {
            std::cerr << line << std::endl;
        } else {
            std::cout << line << std::endl;
        }
    }

    memory_.push_back(std::move(entry));
    while (memory_.size() > kMemoryLines) {
        memory_.pop_front();
    }
}

LogLevel Logger::Level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

std::string Logger::FilePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return path_;
}

std::vector<LogEntry> Logger::EntriesSince(std::uint64_t afterSeq, size_t max) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LogEntry> out;
    for (const LogEntry& e : memory_) {
        if (e.seq > afterSeq) {
            out.push_back(e);
        }
    }
    if (out.size() > max) {
        out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(max));
    }
    return out;
}

std::uint64_t Logger::LastSeq() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nextSeq_ - 1;
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
