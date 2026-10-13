#include "ModemInstance.h"

namespace voipmodem {

ModemInstance::ModemInstance(ModemInstanceConfig config, Logger& sharedLogger)
    : config_(std::move(config)),
      logger_(sharedLogger, config_.name),
      serialPort_(logger_),
      callController_(logger_),
      core_(callController_, logger_, config_.modemIdentification),
      session_(serialPort_, core_, logger_) {}

bool ModemInstance::Open() {
    return serialPort_.Open(config_.comPort, config_.baudRate);
}

void ModemInstance::Start() {
    thread_ = std::thread(&ModemInstance::RunLoop, this);
}

void ModemInstance::Stop() {
    stopRequested_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
    serialPort_.Close();
}

void ModemInstance::RunLoop() {
    logger_.Info("Listening on " + config_.comPort + " at " + std::to_string(config_.baudRate) +
                 " baud. Identification string: " + config_.modemIdentification);

    while (serialPort_.IsOpen() && !stopRequested_) {
        uint8_t byte = 0;
        if (serialPort_.ReadByte(byte)) {
            session_.OnByteReceived(byte);
        }
        // A false return from ReadByte() is the expected outcome of the
        // port's read timeout when nothing arrived; IsOpen() is what
        // detects an actually closed/failed port.
    }
}

} // namespace voipmodem
