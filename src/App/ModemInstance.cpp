#include "ModemInstance.h"

#include "../Lib/Core/NullCallController.h"
#include "../Lib/Sip/PjsipCallController.h"

namespace voipmodem {

namespace {

std::unique_ptr<ICallController> MakeCallController(const ModemInstanceConfig& config,
                                                     PjsipEndpoint* sipEndpoint, TaggedLogger& logger) {
    if (sipEndpoint == nullptr || config.sipServer.empty()) {
        return std::make_unique<NullCallController>(logger);
    }

    SipAccountConfig sipConfig;
    sipConfig.server = config.sipServer;
    sipConfig.extension = config.sipExtension.empty() ? config.name : config.sipExtension;
    sipConfig.password = config.sipPassword;
    sipConfig.realm = config.sipRealm;

    auto controller = std::make_unique<PjsipCallController>(*sipEndpoint, std::move(sipConfig), logger);
    if (!controller->Start()) {
        logger.Error("SIP account failed to start; falling back to no telephony backend for this instance.");
        return std::make_unique<NullCallController>(logger);
    }
    return controller;
}

} // namespace

ModemInstance::ModemInstance(ModemInstanceConfig config, Logger& sharedLogger, PjsipEndpoint* sipEndpoint)
    : config_(std::move(config)),
      logger_(sharedLogger, config_.name),
      sipEndpoint_(sipEndpoint),
      serialPort_(logger_),
      callController_(MakeCallController(config_, sipEndpoint, logger_)),
      core_(*callController_, logger_, config_.modemIdentification),
      session_(serialPort_, core_, logger_) {
    core_.InitializeAutoAnswerRegister(config_.autoAnswerRingCount);
}

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
    if (sipEndpoint_ != nullptr) {
        sipEndpoint_->RegisterCurrentThread("ModemInstance-" + config_.name);
    }

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

        PollCallEvents();
        PumpIncomingCallRinging();
    }
}

void ModemInstance::PollCallEvents() {
    for (;;) {
        const CallEvent event = callController_->PollEvent();
        if (event.type == CallEventType::None) {
            return;
        }
        session_.SendUnsolicited(core_.HandleCallEvent(event));
    }
}

void ModemInstance::PumpIncomingCallRinging() {
    if (!core_.HasIncomingCall()) {
        ringCount_ = 0;
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (ringCount_ > 0 && now - lastRingAt_ < kRingInterval) {
        return;
    }

    session_.SendUnsolicitedRing();
    lastRingAt_ = now;
    ++ringCount_;

    const int autoAnswerAfter = core_.Registers().Get(0); // S0
    if (autoAnswerAfter > 0 && ringCount_ >= autoAnswerAfter) {
        logger_.Info("Auto-answering after " + std::to_string(ringCount_) + " ring(s) (S0=" +
                     std::to_string(autoAnswerAfter) + ")");
        session_.TriggerAutoAnswer();
    }
}

} // namespace voipmodem
