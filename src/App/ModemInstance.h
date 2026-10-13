#pragma once

#include <atomic>
#include <thread>

#include "CommandLineSession.h"
#include "../Lib/Config/AppConfig.h"
#include "../Lib/Core/ModemStateMachine.h"
#include "../Lib/Core/NullCallController.h"
#include "../Lib/Logging/Logger.h"
#include "../Lib/Logging/TaggedLogger.h"
#include "../Lib/Serial/WindowsSerialPort.h"

namespace voipmodem {

// One independently-running modem line: its own COM port, call
// controller, and state machine, driven by its own thread so that
// multiple instances can run concurrently without one instance's serial
// read blocking another's. See docs/ARCHITECTURE.md "Multiple modem
// instances". Each instance logs through the same shared Logger (which
// is already mutex-protected) tagged with its own name via TaggedLogger,
// so output from several instances stays distinguishable in one log.
class ModemInstance {
public:
    ModemInstance(ModemInstanceConfig config, Logger& sharedLogger);

    // Opens the serial port. Returns false (and logs why) on failure;
    // the caller decides whether that's fatal for the whole process or
    // just means this one instance is skipped.
    bool Open();

    // Starts the read loop on a background thread. Call only after a
    // successful Open().
    void Start();

    // Signals the read loop to stop, joins the thread, and closes the
    // port. Safe to call even if Start() was never called.
    void Stop();

    const std::string& Name() const { return config_.name; }

private:
    void RunLoop();

    ModemInstanceConfig config_;
    TaggedLogger logger_;
    WindowsSerialPort serialPort_;
    NullCallController callController_;
    ModemStateMachine core_;
    CommandLineSession session_;
    std::atomic<bool> stopRequested_{false};
    std::thread thread_;
};

} // namespace voipmodem
