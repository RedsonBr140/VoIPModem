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

// How long a line shows "No carrier" after a call that never connected.
constexpr std::chrono::seconds kFailedCallDisplay{15};
constexpr std::chrono::milliseconds kStatusInterval{500};

// The SIP account is created here but only started (registered) by
// ModemInstance::Open(), once the COM port is open.
std::unique_ptr<ICallController> MakeCallController(const ModemInstanceConfig& config, PjsipEndpoint* sipEndpoint,
                                                     const LineRuntimeSettings& runtime, TaggedLogger& logger,
                                                     PjsipCallController*& sipOut) {
    sipOut = nullptr;
    if (sipEndpoint == nullptr || config.sipServer.empty()) {
        return std::make_unique<NullCallController>(logger);
    }

    SipAccountConfig sipConfig;
    sipConfig.server = config.sipServer;
    sipConfig.extension = config.sipExtension.empty() ? config.name : config.sipExtension;
    sipConfig.password = config.sipPassword;
    sipConfig.realm = config.sipRealm;
    sipConfig.enableLocalAudio = config.enableLocalAudio;
    sipConfig.modulations = config.modulations;
    sipConfig.errorControl = config.errorControl;
    sipConfig.compression = config.compression;
    sipConfig.transport = runtime.sipTransport;
    sipConfig.registerExpirySeconds = runtime.sipRegisterExpiry;
    sipConfig.registerOnStart = runtime.sipRegisterOnStart && config.inService;
    sipConfig.rtpPort = runtime.rtpPort;
    sipConfig.rtpPortRange = runtime.rtpPortRange;

    auto controller = std::make_unique<PjsipCallController>(*sipEndpoint, std::move(sipConfig), logger);
    sipOut = controller.get();
    return controller;
}

const char* RegistrationText(CallStatus::Registration r) {
    switch (r) {
        case CallStatus::Registration::Registering: return "registering";
        case CallStatus::Registration::Registered: return "registered";
        case CallStatus::Registration::Failed: return "failed";
        case CallStatus::Registration::Unregistered: return "unregistered";
        default: return "none";
    }
}

} // namespace

ModemInstance::ModemInstance(ModemInstanceConfig config, Logger& sharedLogger, PjsipEndpoint* sipEndpoint,
                             const LineRuntimeSettings& runtime, ServiceStats& stats)
    : config_(std::move(config)),
      runtime_(runtime),
      stats_(stats),
      logger_(sharedLogger, config_.name),
      sipEndpoint_(sipEndpoint),
      serialPort_(logger_),
      callController_(MakeCallController(config_, sipEndpoint, runtime_, logger_, sip_)),
      core_(*callController_, logger_, config_.modemIdentification),
      session_(serialPort_, core_, logger_) {
    core_.InitializeAutoAnswerRegister(config_.autoAnswerRingCount);
    session_.SetTrace(runtime_.traceAtCommands);
    if (config_.enableLocalAudio) {
        dialAudio_.emplace();
    }
}

ModemInstance::~ModemInstance() {
    Stop();
}

bool ModemInstance::Open() {
    if (config_.comPort.empty()) {
        logger_.Error("No ComPort= in this line's section: it stays closed until one is set.");
        RefreshStatus();
        return false;
    }
    if (!serialPort_.Open(config_.comPort, config_.baudRate)) {
        RefreshStatus();
        return false;
    }
    dtrOut_.reset();
    UpdateCarrierOutput();
    bool dteDtr = true; // if DSR cannot be read, behave as before: DTE present
    serialPort_.ReadDsr(dteDtr);
    dtrIn_.Reset(dteDtr);
    core_.SetDteReady(dteDtr);
    logger_.Debug(std::string("DTE DTR (our DSR) is ") + (dteDtr ? "on" : "off") + " at open");
    if (sip_ != nullptr && !sipStarted_) {
        sipStarted_ = sip_->Start();
        if (!sipStarted_) {
            logger_.Error("SIP account failed to start; this line has no telephony until it is reset.");
        }
    }
    RefreshStatus();
    return true;
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

void ModemInstance::Post(LineCommand command) {
    std::lock_guard<std::mutex> lock(commandMutex_);
    commands_.push_back(command);
}

LineStatus ModemInstance::Status() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    return status_;
}

bool ModemInstance::InCall() const {
    return inCall_;
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
            // as an AT command, except for a genuine "+++" escape
            // sequence (see HandleOnlineDataByte()/EscapeDetectState).
            if (core_.CurrentState() == ModemState::OnlineData) {
                HandleOnlineDataByte(byte);
            } else {
                escapeState_ = EscapeDetectState::Idle; // stale state must never survive into the next call
                session_.OnByteReceived(byte);
                ObserveState();
            }
        }
        // A false return from ReadByte() is the expected outcome of the
        // port's read timeout when nothing arrived; IsOpen() is what
        // detects an actually closed/failed port.

        ProcessCommands();
        PumpControlSignals();
        PumpEscapeSequenceTimeout();
        PumpOnlineDataRx();
        PollCallEvents();
        PumpIncomingCallRinging();
        PumpDialAudioTrigger();

        const auto now = std::chrono::steady_clock::now();
        if (wantRegistered_.has_value() && now - lastRegistrationAttempt_ >= std::chrono::seconds(2)) {
            TryRegistration();
        }
        if (now - lastStatusRefresh_ >= kStatusInterval) {
            RefreshStatus();
        }
    }
    RefreshStatus();
}

void ModemInstance::ProcessCommands() {
    std::deque<LineCommand> pending;
    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        pending.swap(commands_);
    }
    for (LineCommand command : pending) {
        const bool callExists =
            call_.active || core_.HasIncomingCall() || core_.CurrentState() != ModemState::CommandMode;
        switch (command) {
            case LineCommand::Answer:
                if (core_.HasIncomingCall()) {
                    logger_.Info("Answering on the operator's request");
                    session_.TriggerAutoAnswer();
                    ObserveState();
                }
                break;
            case LineCommand::HangUp:
                if (callExists) {
                    logger_.Info("Hanging up on the operator's request");
                    callController_->HangUp();
                }
                break;
            case LineCommand::InService:
                config_.inService = true;
                logger_.Info("Line put in service");
                RequestRegistration(true);
                break;
            case LineCommand::OutOfService:
                config_.inService = false;
                logger_.Info("Line taken out of service");
                if (callExists) {
                    callController_->HangUp();
                }
                RequestRegistration(false);
                break;
            case LineCommand::Register:
                if (config_.inService) {
                    RequestRegistration(true);
                }
                break;
            case LineCommand::Unregister:
                RequestRegistration(false);
                break;
        }
    }
    if (!pending.empty()) {
        RefreshStatus();
    }
}

void ModemInstance::RequestRegistration(bool registered) {
    wantRegistered_ = registered;
    TryRegistration();
}

// PJSIP refuses a (un)registration while a REGISTER is in flight; keep
// asking (every 2 s, from RunLoop) until it takes it.
void ModemInstance::TryRegistration() {
    if (!wantRegistered_.has_value()) {
        return;
    }
    lastRegistrationAttempt_ = std::chrono::steady_clock::now();
    if (callController_->SetRegistered(*wantRegistered_)) {
        wantRegistered_.reset();
    }
}

void ModemInstance::ObserveState() {
    const ModemState state = core_.CurrentState();
    if (state == statsState_) {
        return;
    }
    if (state == ModemState::Dialing && !call_.active) {
        call_ = CallTrack{};
        call_.active = true;
        call_.start = std::chrono::steady_clock::now();
        lastCallFailed_ = false;
        stats_.OutboundDialled();
    } else if (state == ModemState::Answering && call_.active && call_.incoming && !call_.answered) {
        call_.answered = true;
        stats_.InboundAnswered();
    }
    statsState_ = state;
}

void ModemInstance::ObserveEvent(const CallEvent& event) {
    ObserveState();
    const auto now = std::chrono::steady_clock::now();
    switch (event.type) {
        case CallEventType::IncomingCall:
            call_ = CallTrack{};
            call_.active = true;
            call_.incoming = true;
            call_.start = now;
            lastCallFailed_ = false;
            stats_.IncomingOffered();
            break;
        case CallEventType::Connected:
            if (call_.active && !call_.connected) {
                call_.connected = true;
                call_.connectedAt = now;
                call_.lastModem = callController_->GetStatus().modem;
                stats_.Connected(call_.incoming, call_.lastModem.modulation, call_.lastModem.txBps,
                                 call_.lastModem.rxBps);
            }
            break;
        case CallEventType::Ended:
            FinishCall();
            break;
        default:
            break;
    }
}

void ModemInstance::FinishCall() {
    if (!call_.active) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (call_.connected) {
        const double seconds = std::chrono::duration<double>(now - call_.connectedAt).count();
        stats_.CallCompleted(seconds, call_.lastModem.retrains, call_.lastModem.renegotiations);
        lastCallFailed_ = false;
    } else {
        if (call_.incoming && call_.answered) {
            stats_.FailedToTrain();
        } else if (call_.incoming) {
            stats_.CallerHungUpEarly();
        }
        // Ours to report: a call we answered or dialled that never connected.
        lastCallFailed_ = call_.answered || !call_.incoming;
        lastFailedEndAt_ = now;
    }
    call_.active = false;
}

void ModemInstance::RefreshStatus() {
    const auto now = std::chrono::steady_clock::now();
    lastStatusRefresh_ = now;

    const CallStatus cs = callController_->GetStatus();
    if (cs.rejectedBusy > reportedRejectedBusy_) {
        stats_.RejectedBusy(cs.rejectedBusy - reportedRejectedBusy_);
        reportedRejectedBusy_ = cs.rejectedBusy;
    }
    const unsigned long atCommands = session_.CommandCount();
    if (atCommands > reportedAtCommands_) {
        stats_.AtCommands(atCommands - reportedAtCommands_);
        reportedAtCommands_ = atCommands;
    }
    if (call_.active && cs.modem.active) {
        call_.lastModem = cs.modem;
    }

    LineStatus s;
    s.id = config_.name;
    s.comPort = config_.comPort;
    s.extension = config_.sipExtension.empty() ? config_.name : config_.sipExtension;
    s.server = config_.sipServer;
    s.portOpen = serialPort_.IsOpen();
    s.inService = config_.inService;
    s.autoAnswerRings = core_.Registers().Get(0);
    s.registration = sip_ != nullptr ? RegistrationText(cs.registration) : "none";
    s.registrationCode = cs.registrationCode;

    const ModemState state = core_.CurrentState();
    const bool ringing = core_.HasIncomingCall();
    s.canAnswer = ringing;
    s.canHangUp = call_.active || ringing || state != ModemState::CommandMode;

    if (call_.active) {
        s.direction = call_.incoming ? "in" : "out";
        s.remoteParty = cs.remoteParty;
        s.callSeconds = std::chrono::duration_cast<std::chrono::seconds>(now - call_.start).count();
        s.bytesTx = call_.bytesTx;
        s.bytesRx = call_.bytesRx;
        s.codec = cs.codec;
        const ModemLinkStatus& m = cs.modem.active ? cs.modem : call_.lastModem;
        if (m.connected) {
            s.modulation = m.modulation;
            s.txBps = m.txBps;
            s.rxBps = m.rxBps;
        } else if (cs.mediaActive) {
            s.modulation = "Training";
        }
        s.snrDb = m.snrDb10 >= 0 ? m.snrDb10 / 10.0 : -1;
        s.retrains = m.retrains;
        s.errors = m.frameErrors;
        s.errorControl = m.errorControl;
        s.compression = m.compression;
        s.jitterMeanMs = cs.jitterMeanMs;
        s.jitterMaxMs = cs.jitterMaxMs;
        s.rtpPacketsReceived = cs.rtpPacketsReceived;
        s.rtpPacketsLost = cs.rtpPacketsLost;
        s.rtpBytes = cs.rtpBytesReceived + cs.rtpBytesSent;
    }

    auto set = [&s](const char* text, const char* kind) {
        s.state = text;
        s.kind = kind;
    };
    if (!s.portOpen) {
        if (config_.comPort.empty()) {
            set("No COM port", "err");
            s.fault = "ComPort is not set for this line";
        } else {
            set("Port closed", "err");
            s.fault = "cannot open " + config_.comPort;
        }
    } else if (state == ModemState::OnlineData) {
        set("Connected", "up");
    } else if (call_.active && call_.connected && state == ModemState::CommandMode) {
        set("Online command", "up"); // "+++" with the call still up
    } else if (ringing) {
        set("Ringing", "neg");
    } else if (state == ModemState::Answering) {
        set(cs.mediaActive ? "Handshake" : "Answering", "neg");
    } else if (state == ModemState::Dialing) {
        set(cs.mediaActive ? "Handshake" : "Dialing", "neg");
    } else if (state == ModemState::Disconnecting || state == ModemState::Negotiating) {
        set(state == ModemState::Negotiating ? "Handshake" : "Hanging up", "neg");
    } else if (!config_.inService) {
        set("Out of service", "off");
    } else if (sip_ != nullptr && !sipStarted_) {
        set("SIP error", "err");
        s.fault = "the SIP account could not be created";
    } else if (cs.registration == CallStatus::Registration::Failed) {
        set("Not registered", "err");
        s.fault = "SIP registration failed: " + std::to_string(cs.registrationCode) + " " + cs.registrationReason;
    } else if (lastCallFailed_ && now - lastFailedEndAt_ < kFailedCallDisplay) {
        set("No carrier", "err");
    } else if (cs.registration == CallStatus::Registration::Unregistered) {
        set("Unregistered", "off");
    } else if (sip_ == nullptr) {
        set("No SIP", "off");
        s.fault = "no SipServer (or no [SIP] LocalPort): this line cannot place or take calls";
    } else {
        set(s.autoAnswerRings > 0 ? "Waiting" : "Idle (AT)", "idle");
    }

    inCall_ = s.canHangUp;
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_ = std::move(s);
}

void ModemInstance::PumpOnlineDataRx() {
    if (core_.CurrentState() != ModemState::OnlineData) {
        return;
    }
    const std::vector<uint8_t> received = callController_->PollReceivedData();
    if (!received.empty()) {
        serialPort_.Write(received);
        call_.bytesRx += received.size();
        stats_.Bytes(received.size(), 0);
    }
}

void ModemInstance::SendToCall(const std::vector<std::uint8_t>& bytes) {
    callController_->SendData(bytes);
    call_.bytesTx += bytes.size();
    stats_.Bytes(0, bytes.size());
}

void ModemInstance::HandleOnlineDataByte(std::uint8_t byte) {
    const auto now = std::chrono::steady_clock::now();
    const bool precededBySilence =
        lastOnlineDataByteAt_ == std::chrono::steady_clock::time_point{} ||
        (now - lastOnlineDataByteAt_) >= kEscapeGuardTime;

    switch (escapeState_) {
        case EscapeDetectState::Idle:
            if (byte == '+' && precededBySilence) {
                escapeState_ = EscapeDetectState::SawPlus1;
                lastOnlineDataByteAt_ = now;
                return; // held back -- might be the start of a real escape
            }
            break;

        case EscapeDetectState::SawPlus1:
            if (byte == '+' && !precededBySilence) {
                escapeState_ = EscapeDetectState::SawPlus2;
                lastOnlineDataByteAt_ = now;
                return;
            }
            FlushHeldPluses(1);
            escapeState_ = EscapeDetectState::Idle;
            break;

        case EscapeDetectState::SawPlus2:
            if (byte == '+' && !precededBySilence) {
                escapeState_ = EscapeDetectState::SawPlus3PendingSilence;
                thirdPlusAt_ = now;
                lastOnlineDataByteAt_ = now;
                return; // now waiting for post-guard silence; see PumpEscapeSequenceTimeout()
            }
            FlushHeldPluses(2);
            escapeState_ = EscapeDetectState::Idle;
            break;

        case EscapeDetectState::SawPlus3PendingSilence:
            // A real escape needs silence after the third '+' too; this
            // byte arriving too soon means it wasn't one after all.
            FlushHeldPluses(3);
            escapeState_ = EscapeDetectState::Idle;
            break;
    }

    lastOnlineDataByteAt_ = now;
    SendToCall(std::vector<uint8_t>{byte});
}

void ModemInstance::FlushHeldPluses(int count) {
    SendToCall(std::vector<uint8_t>(static_cast<size_t>(count), static_cast<uint8_t>('+')));
}

void ModemInstance::PumpEscapeSequenceTimeout() {
    if (core_.CurrentState() != ModemState::OnlineData) {
        escapeState_ = EscapeDetectState::Idle;
        return;
    }
    if (escapeState_ != EscapeDetectState::SawPlus3PendingSilence) {
        return;
    }
    if (std::chrono::steady_clock::now() - thirdPlusAt_ < kEscapeGuardTime) {
        return; // still within the guard window; a byte might yet arrive and disqualify it
    }

    // Guard time held both before the first '+' and after the third:
    // a real escape. The "+++" itself is never sent to the far end, same
    // as a real modem -- only the state transition happens here, the
    // call and its data link stay up untouched.
    escapeState_ = EscapeDetectState::Idle;
    core_.EnterCommandModeFromOnlineData();
    logger_.Info("+++ escape sequence detected; back to command mode (call still up)");
    ExecutionOutcome outcome;
    outcome.suppressFinalResult = core_.Profile().quietMode;
    outcome.finalResult = ResultCode::OK;
    session_.SendUnsolicited(outcome);
}

void ModemInstance::PollCallEvents() {
    for (;;) {
        const CallEvent event = callController_->PollEvent();
        if (event.type == CallEventType::None) {
            return;
        }
        ObserveEvent(event);
        const ExecutionOutcome outcome = core_.HandleCallEvent(event);
        // DCD goes up before CONNECT and down before NO CARRIER, as on a
        // real modem: a DTE may check DCD as soon as it reads the result.
        UpdateCarrierOutput();
        session_.SendUnsolicited(outcome);
        ObserveState();
    }
}

void ModemInstance::PumpControlSignals() {
    bool dsr = false;
    if (serialPort_.ReadDsr(dsr)) {
        switch (dtrIn_.Update(dsr, std::chrono::steady_clock::now())) {
            case DtrDebouncer::Change::Raised:
                logger_.Debug("DTE raised DTR (our DSR on)");
                core_.HandlePeerDtrChange(true);
                break;
            case DtrDebouncer::Change::Dropped: {
                logger_.Debug("DTE dropped DTR (our DSR off)");
                const DtrOutcome outcome = core_.HandlePeerDtrChange(false);
                UpdateCarrierOutput();
                session_.SendUnsolicited(outcome.response);
                ObserveState();
                break;
            }
            case DtrDebouncer::Change::None:
                break;
        }
    }
    UpdateCarrierOutput(); // picks up ATH, AT&C, ATZ and the like
}

void ModemInstance::UpdateCarrierOutput() {
    const bool want = core_.CarrierAsserted();
    if (dtrOut_.has_value() && *dtrOut_ == want) {
        return;
    }
    // Recorded even if the set fails, so a refusing driver is reported
    // once (WindowsSerialPort logs it) rather than every iteration.
    dtrOut_ = want;
    if (serialPort_.SetDtr(want)) {
        logger_.Debug(std::string("Carrier ") + (want ? "on" : "off") + ": our DTR (the DTE's DCD) " +
                      (want ? "raised" : "lowered"));
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
    if (autoAnswerAfter > 0 && ringCount_ >= autoAnswerAfter && !core_.AutoAnswerAllowed()) {
        if (ringCount_ == autoAnswerAfter) {
            logger_.Debug("Not auto-answering: the DTE's DTR is off (AT&D" +
                          std::to_string(core_.Profile().dtrMode) + ")");
        }
    } else if (autoAnswerAfter > 0 && ringCount_ >= autoAnswerAfter) {
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
