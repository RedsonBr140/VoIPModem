#pragma once

#include <string>

#include "Logger.h"

namespace voipmodem {

// Thin forwarding wrapper that prefixes every message with a fixed tag
// before delegating to a shared Logger. Logger's own mutex already makes
// concurrent writes from multiple threads safe; this exists purely so
// that multiple concurrently-running ModemInstance objects (see
// docs/ARCHITECTURE.md "Multiple modem instances") stay distinguishable
// in one shared log file instead of needing one log file per instance.
// Core/Serial/Hayes-adjacent components (ModemStateMachine,
// NullCallController, WindowsSerialPort, CommandLineSession) depend on
// this instead of Logger directly for exactly that reason — they have no
// idea whether they're the only modem instance or one of several.
class TaggedLogger {
public:
    TaggedLogger(Logger& logger, std::string tag) : logger_(logger), tag_(std::move(tag)) {}

    void Error(const std::string& message) { logger_.Error(Prefix(message)); }
    void Warn(const std::string& message) { logger_.Warn(Prefix(message)); }
    void Info(const std::string& message) { logger_.Info(Prefix(message)); }
    void Debug(const std::string& message) { logger_.Debug(Prefix(message)); }

private:
    std::string Prefix(const std::string& message) const {
        return tag_.empty() ? message : ("[" + tag_ + "] " + message);
    }

    Logger& logger_;
    std::string tag_;
};

} // namespace voipmodem
