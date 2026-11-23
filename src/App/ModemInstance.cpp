#include "ModemInstance.h"

#include <vector>

#include "../Lib/Core/NullCallController.h"
#include "../Lib/Dsp/DtmfGenerator.h"
#include "../Lib/Sip/PjsipCallController.h"

namespace voipmodem {

namespace {

constexpr dsp::Sample kDialAudioAmplitude = 0.8f;
constexpr double kDialToneSeconds = 1.0;
constexpr double kDtmfDigitSeconds = 0.08;
constexpr double kDtmfGapSeconds = 0.08;

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
    sipConfig.enableV34Probing = config.enableV34Probing;
    sipConfig.enableLocalAudio = config.enableLocalAudio;

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
    if (config_.enableLocalAudio) {
        dialAudio_.emplace();
    }
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
    if (dialAudioThread_.joinable()) {
        dialAudioThread_.join();
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
            // Online data mode (see docs/ROADMAP.md Phase 4's V.21
            // milestone): bytes typed into the serial port go straight
            // to the active call's modem link instead of being parsed
            // as an AT command. There is deliberately no +++ escape
            // sequence back to command mode yet -- the only way out is
            // the call ending (ATH, or the far end hanging up); see
            // ModemState.h's own notes on what Phase 4 does and does
            // not implement.
            if (core_.CurrentState() == ModemState::OnlineData) {
                callController_->SendData(std::vector<uint8_t>{byte});
            } else {
                session_.OnByteReceived(byte);
            }
        }
        // A false return from ReadByte() is the expected outcome of the
        // port's read timeout when nothing arrived; IsOpen() is what
        // detects an actually closed/failed port.

        PumpOnlineDataRx();
        PollCallEvents();
        PumpIncomingCallRinging();
        PumpDialAudioTrigger();
    }
}

void ModemInstance::PumpOnlineDataRx() {
    if (core_.CurrentState() != ModemState::OnlineData) {
        return;
    }
    const std::vector<uint8_t> received = callController_->PollReceivedData();
    if (!received.empty()) {
        serialPort_.Write(received);
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

void ModemInstance::PumpDialAudioTrigger() {
    const ModemState current = core_.CurrentState();
    const bool justStartedDialing = current == ModemState::Dialing && lastObservedState_ != ModemState::Dialing;
    lastObservedState_ = current;

    if (!justStartedDialing || !dialAudio_.has_value()) {
        return;
    }

    // A redial before the previous dial sequence's ~1-3 seconds finish
    // is an unusual edge case (the user mashing ATD again); joining
    // here briefly blocks the main loop rather than overlapping two
    // dial-sound sequences on the same WindowsAudioOutput instance.
    if (dialAudioThread_.joinable()) {
        dialAudioThread_.join();
    }
    dialAudioThread_ = std::thread(&ModemInstance::PlayDialSequence, this, core_.LastDialedNumber());
}

void ModemInstance::PlayDialSequence(std::string number) {
    if (!dialAudio_.has_value()) {
        return;
    }

    std::vector<dsp::Sample> dialTone;
    dsp::GenerateDialTone(kDialToneSeconds, kDialAudioAmplitude, dialTone);
    dialAudio_->Write(dialTone);
    std::this_thread::sleep_for(std::chrono::duration<double>(kDialToneSeconds));

    for (char digit : number) {
        if (stopRequested_) {
            return;
        }
        if (!dsp::GetDtmfFrequencies(digit).has_value()) {
            continue; // not a DTMF character (e.g. a pause comma); just skip it
        }
        std::vector<dsp::Sample> digitTone;
        dsp::GenerateDtmfDigit(digit, kDtmfDigitSeconds, kDialAudioAmplitude, digitTone);
        dialAudio_->Write(digitTone);
        std::this_thread::sleep_for(std::chrono::duration<double>(kDtmfDigitSeconds + kDtmfGapSeconds));
    }
}

} // namespace voipmodem
