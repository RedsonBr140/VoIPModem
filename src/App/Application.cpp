#include "Application.h"

#include <atomic>

namespace voipmodem {

namespace {

std::atomic<bool> g_stopRequested{false};

AppConfig LoadConfig(const std::string& path) {
    IniConfig ini;
    ini.LoadFromFile(path); // missing/unreadable file just leaves defaults in place
    return LoadAppConfig(ini);
}

} // namespace

void RequestStop() {
    g_stopRequested = true;
}

Application::Application(std::string configPath)
    : configPath_(std::move(configPath)),
      config_(LoadConfig(configPath_)),
      serialPort_(logger_),
      callController_(logger_),
      core_(callController_, logger_, config_.modemIdentification),
      session_(serialPort_, core_, logger_) {}

int Application::Run() {
    logger_.Init(config_.logLevel, config_.logFilePath);
    logger_.Info("VoIPModem starting (Phase 1: Hayes command skeleton, no SIP/DSP backend yet)");

    if (!serialPort_.Open(config_.comPort, config_.baudRate)) {
        logger_.Error("Unable to open " + config_.comPort + "; exiting.");
        return 1;
    }

    logger_.Info("Listening on " + config_.comPort + " at " + std::to_string(config_.baudRate) +
                 " baud. Identification string: " + config_.modemIdentification);

    while (serialPort_.IsOpen() && !g_stopRequested) {
        uint8_t byte = 0;
        if (serialPort_.ReadByte(byte)) {
            session_.OnByteReceived(byte);
        }
        // A false return from ReadByte() is the expected outcome of the
        // port's read timeout when nothing arrived; IsOpen() is what
        // detects an actually closed/failed port.
    }

    logger_.Info("VoIPModem shutting down");
    serialPort_.Close();
    return 0;
}

} // namespace voipmodem
