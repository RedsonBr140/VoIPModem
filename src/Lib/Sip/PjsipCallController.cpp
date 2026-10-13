#include "PjsipCallController.h"

#include <ctime>
#include <pjsua2.hpp> // must precede Windows.h: pulls in winsock2.h before windows.h can pull in the conflicting legacy winsock.h

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace voipmodem {

namespace {

// Directory the running .exe lives in, with a trailing backslash — used
// to resolve assets/output files relative to the binary rather than
// whatever the current working directory happens to be when launched.
std::string GetExecutableDirectory() {
    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string full(path, length);
    const size_t lastSlash = full.find_last_of("\\/");
    return lastSlash == std::string::npos ? "" : full.substr(0, lastSlash + 1);
}

// SIP final-status -> Hayes result code. This is a deliberately small,
// documented approximation (see docs/ARCHITECTURE.md "SIP integration
// (Phase 2)"), not a precise protocol mapping: most real modems only have
// a handful of failure result codes anyway, so most SIP failures collapse
// onto NO CARRIER, exactly as a real modem reports a connection that
// never came up or that dropped, for whatever reason.
ResultCode MapSipStatusToResultCode(pjsip_status_code code) {
    switch (code) {
        case PJSIP_SC_BUSY_HERE:
        case PJSIP_SC_BUSY_EVERYWHERE:
        case PJSIP_SC_DECLINE:
            return ResultCode::Busy;
        case PJSIP_SC_TEMPORARILY_UNAVAILABLE:
        case PJSIP_SC_REQUEST_TIMEOUT:
        case PJSIP_SC_REQUEST_TERMINATED:
        case PJSIP_SC_SERVER_TIMEOUT:
            return ResultCode::NoAnswer;
        default:
            return ResultCode::NoCarrier;
    }
}

} // namespace

class SipAccount;
class SipCall;

// Shared state between PjsipCallController and the nested pjsua2
// Account/Call subclasses below, which can't be members of
// PjsipCallController directly (pjsua2 callbacks need a stable address
// reachable from pjsua2's own C callback plumbing, and this keeps pjsua2
// types from leaking into PjsipCallController.h). `mutex_` guards
// everything here, since Account/Call callbacks fire on PJSIP's own
// worker thread while PollEvent()/Dial()/Answer()/HangUp() are called
// from a ModemInstance's thread.
class PjsipCallController::Impl {
public:
    explicit Impl(TaggedLogger& logger);
    ~Impl();

    void PushEvent(const CallEvent& event);
    CallEvent PopEvent();

    // Moves whichever of activeCall_/pendingIncomingCall_ is `which` into
    // zombies_ instead of deleting it immediately. pjsua2's own samples
    // avoid "delete this" from inside a Call's own onCallState callback
    // (the call object may still be on PJSIP's stack at that point);
    // deferring actual destruction to the next PopEvent() call — always
    // on a different thread, always after this callback has returned —
    // avoids that hazard.
    void RetireCall(SipCall* which);

    TaggedLogger& logger_;
    std::string instanceLabel_; // e.g. the extension; used to name recording files
    std::mutex mutex_;
    std::deque<CallEvent> events_;
    std::vector<std::unique_ptr<pj::Call>> zombies_;
    std::unique_ptr<SipAccount> account_;
    std::unique_ptr<SipCall> activeCall_;
    std::unique_ptr<SipCall> pendingIncomingCall_;
};

class SipCall : public pj::Call {
public:
    SipCall(pj::Account& account, PjsipCallController::Impl& impl, int callId = PJSUA_INVALID_ID)
        : pj::Call(account, callId), impl_(impl) {}

    void onCallState(pj::OnCallStateParam& prm) override {
        PJ_UNUSED_ARG(prm);

        pj::CallInfo info;
        try {
            info = getInfo();
        } catch (const pj::Error&) {
            return; // call already torn down underneath us; nothing to report
        }

        if (info.state == PJSIP_INV_STATE_CONFIRMED) {
            impl_.PushEvent(CallEvent{CallEventType::Connected});
        } else if (info.state == PJSIP_INV_STATE_DISCONNECTED) {
            impl_.PushEvent(CallEvent{CallEventType::Ended, MapSipStatusToResultCode(info.lastStatusCode)});
            impl_.RetireCall(this);
        }
    }

    // Phase 3: wire this call's audio to a WAV recorder (RX) and a looping
    // test-tone WAV player (TX) so audio actually flows through the call
    // without needing any physical sound device — see PjsipEndpoint::Start()
    // for the codec/AEC/VAD configuration that keeps this path from
    // mangling modem tones. This replaces an earlier live-sound-device
    // version: PJSIP's device APIs (both the default-device lookup and
    // ExtraAudioDevice) hardcode PJMEDIA_DIR_CAPTURE_PLAYBACK, so neither
    // works at all on a machine with zero capture devices (confirmed to be
    // the case on the dev machine via waveInGetNumDevs() == 0 — not a
    // VoIPModem bug, a real PJSIP/hardware constraint). Phase 4 replaces
    // this WAV-based sink with the DSP engine; nothing else about call
    // handling needs to change when that happens.
    void onCallMediaState(pj::OnCallMediaStateParam& prm) override {
        PJ_UNUSED_ARG(prm);

        pj::CallInfo info;
        try {
            info = getInfo();
        } catch (const pj::Error&) {
            return;
        }

        for (unsigned i = 0; i < info.media.size(); ++i) {
            if (info.media[i].type != PJMEDIA_TYPE_AUDIO || info.media[i].status != PJSUA_CALL_MEDIA_ACTIVE) {
                continue;
            }

            pj::Media* media = getMedia(i);
            auto* audioMedia = dynamic_cast<pj::AudioMedia*>(media);
            if (audioMedia == nullptr) {
                continue;
            }

            if (!recorder_) {
                try {
                    recorder_ = std::make_unique<pj::AudioMediaRecorder>();
                    recorder_->createRecorder(BuildRecordingPath());
                    audioMedia->startTransmit(*recorder_);
                } catch (const pj::Error& err) {
                    impl_.logger_.Error("Failed to start recording call audio: " + err.info());
                    recorder_.reset();
                }
            }

            if (!player_) {
                try {
                    player_ = std::make_unique<pj::AudioMediaPlayer>();
                    player_->createPlayer(GetExecutableDirectory() + "test-tone.wav"); // loops by default
                    player_->startTransmit(*audioMedia);
                } catch (const pj::Error& err) {
                    impl_.logger_.Error("Failed to play test tone into call: " + err.info());
                    player_.reset();
                }
            }
        }
    }

private:
    std::string BuildRecordingPath() const {
        return GetExecutableDirectory() + "call-recording-" + impl_.instanceLabel_ + "-" +
               std::to_string(static_cast<long long>(std::time(nullptr))) + ".wav";
    }

    PjsipCallController::Impl& impl_;
    std::unique_ptr<pj::AudioMediaRecorder> recorder_;
    std::unique_ptr<pj::AudioMediaPlayer> player_;
};

class SipAccount : public pj::Account {
public:
    explicit SipAccount(PjsipCallController::Impl& impl) : impl_(impl) {}

    void onRegState(pj::OnRegStateParam& prm) override {
        pj::AccountInfo info = getInfo();
        impl_.logger_.Info(std::string("SIP ") + (info.regIsActive ? "registration" : "unregistration") +
                            " result: " + std::to_string(prm.code) + " " + prm.reason);
    }

    void onIncomingCall(pj::OnIncomingCallParam& iprm) override {
        auto call = std::make_unique<SipCall>(*this, impl_, iprm.callId);

        if (impl_.activeCall_ || impl_.pendingIncomingCall_) {
            // A modem line only ever handles one call at a time; reject
            // rather than queueing a second one.
            pj::CallOpParam rejectParam;
            rejectParam.statusCode = PJSIP_SC_BUSY_HERE;
            try {
                call->hangup(rejectParam);
            } catch (const pj::Error& err) {
                impl_.logger_.Warn("Failed to reject second incoming call: " + err.info());
            }
            return; // `call` destructs here; it was never accepted
        }

        impl_.pendingIncomingCall_ = std::move(call);
        impl_.PushEvent(CallEvent{CallEventType::IncomingCall});
    }

private:
    PjsipCallController::Impl& impl_;
};

PjsipCallController::Impl::Impl(TaggedLogger& logger) : logger_(logger) {}

PjsipCallController::Impl::~Impl() = default;

void PjsipCallController::Impl::PushEvent(const CallEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(event);
}

CallEvent PjsipCallController::Impl::PopEvent() {
    std::lock_guard<std::mutex> lock(mutex_);
    zombies_.clear(); // safe here: never called from inside a pjsua2 callback
    if (events_.empty()) {
        return CallEvent{};
    }
    const CallEvent event = events_.front();
    events_.pop_front();
    return event;
}

void PjsipCallController::Impl::RetireCall(SipCall* which) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (activeCall_.get() == which) {
        zombies_.push_back(std::move(activeCall_));
    } else if (pendingIncomingCall_.get() == which) {
        zombies_.push_back(std::move(pendingIncomingCall_));
    }
}

PjsipCallController::PjsipCallController(PjsipEndpoint& endpoint, SipAccountConfig config, TaggedLogger& logger)
    : endpoint_(endpoint), config_(std::move(config)), logger_(logger), impl_(std::make_unique<Impl>(logger)) {}

PjsipCallController::~PjsipCallController() {
    if (impl_->account_) {
        try {
            impl_->account_->setRegistration(false);
        } catch (const pj::Error& err) {
            logger_.Warn("Unregistration failed during shutdown: " + err.info());
        }
    }
}

bool PjsipCallController::Start() {
    endpoint_.RegisterCurrentThread("PjsipSetup-" + config_.extension);
    impl_->instanceLabel_ = config_.extension;

    try {
        auto account = std::make_unique<SipAccount>(*impl_);

        pj::AccountConfig accountConfig;
        accountConfig.idUri = "sip:" + config_.extension + "@" + config_.server;
        accountConfig.regConfig.registrarUri = "sip:" + config_.server;

        pj::AuthCredInfo cred("digest", config_.realm, config_.extension, 0, config_.password);
        accountConfig.sipConfig.authCreds.push_back(cred);

        account->create(accountConfig);
        impl_->account_ = std::move(account);
        logger_.Info("SIP account created for extension '" + config_.extension + "' against " + config_.server);
        return true;
    } catch (const pj::Error& err) {
        logger_.Error("Failed to create SIP account for extension '" + config_.extension + "': " + err.info());
        return false;
    }
}

void PjsipCallController::Dial(const std::string& number) {
    if (!impl_->account_) {
        logger_.Error("ATD requested but the SIP account was never started.");
        impl_->PushEvent(CallEvent{CallEventType::Ended, ResultCode::NoCarrier});
        return;
    }
    if (impl_->activeCall_) {
        logger_.Warn("ATD requested while a call is already active; ignoring.");
        return;
    }

    try {
        auto call = std::make_unique<SipCall>(*impl_->account_, *impl_);
        pj::CallOpParam callParam(true);
        call->makeCall("sip:" + number + "@" + config_.server, callParam);
        impl_->activeCall_ = std::move(call);
    } catch (const pj::Error& err) {
        logger_.Error("Dial to '" + number + "' failed: " + err.info());
        impl_->PushEvent(CallEvent{CallEventType::Ended, ResultCode::NoCarrier});
    }
}

void PjsipCallController::Answer() {
    if (!impl_->pendingIncomingCall_) {
        logger_.Warn("ATA requested but there is no incoming call to answer.");
        return;
    }

    try {
        pj::CallOpParam answerParam;
        answerParam.statusCode = PJSIP_SC_OK;
        impl_->pendingIncomingCall_->answer(answerParam);
        impl_->activeCall_ = std::move(impl_->pendingIncomingCall_);
    } catch (const pj::Error& err) {
        logger_.Error("Answer failed: " + err.info());
        impl_->pendingIncomingCall_.reset();
        impl_->PushEvent(CallEvent{CallEventType::Ended, ResultCode::NoCarrier});
    }
}

void PjsipCallController::HangUp() {
    try {
        if (impl_->activeCall_) {
            pj::CallOpParam hangupParam;
            impl_->activeCall_->hangup(hangupParam);
        } else if (impl_->pendingIncomingCall_) {
            pj::CallOpParam declineParam;
            declineParam.statusCode = PJSIP_SC_DECLINE;
            impl_->pendingIncomingCall_->hangup(declineParam);
        }
    } catch (const pj::Error& err) {
        logger_.Warn("HangUp: " + err.info());
    }
}

CallEvent PjsipCallController::PollEvent() {
    return impl_->PopEvent();
}

} // namespace voipmodem
