#include "PjsipCallController.h"

#include <pjsua2.hpp> // must precede Windows.h: pulls in winsock2.h before windows.h can pull in the conflicting legacy winsock.h

#include "CallAudioBridge.h"

namespace voipmodem {

namespace {

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

// "\"Name\" <sip:5551204@10.0.0.1;user=phone>" -> "5551204"; the display
// name when the URI has no user part.
std::string RemoteUser(const std::string& uri) {
    const auto sip = uri.find("sip:");
    if (sip != std::string::npos) {
        const auto start = sip + 4;
        const auto at = uri.find('@', start);
        const auto stop = uri.find_first_of(">;", start);
        if (at != std::string::npos && (stop == std::string::npos || at < stop)) {
            return uri.substr(start, at - start);
        }
    }
    const auto q1 = uri.find('"');
    const auto q2 = q1 == std::string::npos ? std::string::npos : uri.find('"', q1 + 1);
    if (q2 != std::string::npos && q2 > q1 + 1) {
        return uri.substr(q1 + 1, q2 - q1 - 1);
    }
    return uri;
}

// "sip:host:port" plus ";transport=tcp" when the line uses TCP.
std::string SipUri(const std::string& userAtHost, const std::string& transport) {
    return "sip:" + userAtHost + (transport == "TCP" ? ";transport=tcp" : "");
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
    bool enableLocalAudio_ = false;
    ModemOptions modemOptions_;

    // Status for GetStatus(), written from PJSIP's thread; under mutex_.
    CallStatus::Registration registration_ = CallStatus::Registration::None;
    int registrationCode_ = 0;
    std::string registrationReason_;
    std::string remoteParty_;
    std::string codec_;
    int audioMediaIndex_ = -1;
    unsigned long rejectedBusy_ = 0;
    // ModemInstance thread only: we already reported NO CARRIER for a
    // modem that ended and hung up ourselves, so the SIP disconnect that
    // follows must not report it a second time.
    bool swallowNextSipEnded_ = false;
};

class SipCall : public pj::Call {
public:
    SipCall(pj::Account& account, PjsipCallController::Impl& impl, bool callingParty, int callId = PJSUA_INVALID_ID)
        : pj::Call(account, callId), impl_(impl), callingParty_(callingParty) {}

    void onCallState(pj::OnCallStateParam& prm) override {
        PJ_UNUSED_ARG(prm);

        pj::CallInfo info;
        try {
            info = getInfo();
        } catch (const pj::Error&) {
            return; // call already torn down underneath us; nothing to report
        }

        {
            std::lock_guard<std::mutex> lock(impl_.mutex_);
            if (impl_.activeCall_.get() == this || impl_.pendingIncomingCall_.get() == this) {
                impl_.remoteParty_ = RemoteUser(info.remoteUri); // not a call we refused as busy
            }
        }

        // SIP CONFIRMED is only the phone call being up, not the modem
        // link: CONNECT is reported once the modem reaches data mode (see
        // PjsipCallController::PollEvent), as a real modem would.
        if (info.state == PJSIP_INV_STATE_DISCONNECTED) {
            impl_.PushEvent(CallEvent{CallEventType::Ended, MapSipStatusToResultCode(info.lastStatusCode)});
            impl_.RetireCall(this);
        }
    }

    // Phase 4's first call-media wiring increment: replaces Phase 3's WAV
    // recorder/player with CallAudioBridge, a custom AudioMediaPort that
    // generates/detects handshake-relevant tones using the src/Lib/Dsp
    // primitives — see CallAudioBridge.h for exactly what this does and
    // does not prove (a tone can be put on the wire and heard back; no
    // handshake, modulation, or data mode yet).
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

            if (!audioBridge_) {
                bool alaw = false; // the modem must know the codec's law (V.90/V.92 PCM especially)
                try {
                    const pj::StreamInfo si = getStreamInfo(i);
                    alaw = si.codecName.rfind("PCMA", 0) == 0;
                    impl_.logger_.Info("Call audio codec: " + si.codecName);
                    std::lock_guard<std::mutex> lock(impl_.mutex_);
                    impl_.codec_ = si.codecName;
                    impl_.audioMediaIndex_ = static_cast<int>(i);
                } catch (const pj::Error&) {
                }
                try {
                    audioBridge_ = std::make_unique<CallAudioBridge>(callingParty_, impl_.logger_,
                                                                      impl_.enableLocalAudio_, alaw,
                                                                      impl_.modemOptions_);
                    audioBridge_->Start(*audioMedia);
                } catch (const pj::Error& err) {
                    impl_.logger_.Error("Failed to start tone-probe audio bridge: " + err.info());
                    audioBridge_.reset();
                }
            }
        }
    }

    // Forwards to audioBridge_ if it exists yet, no-ops otherwise --
    // see ICallController::SendData/PollReceivedData's own comment for
    // why bytes arriving before the bridge (or its data link) exists
    // simply wait rather than being dropped: SendData() only drops a
    // byte if there is no bridge AT ALL yet (media not started), not
    // once there is one queued and waiting.
    void EnqueueTxByte(std::uint8_t byte) {
        if (audioBridge_) {
            audioBridge_->EnqueueTxByte(byte);
        }
    }

    std::vector<std::uint8_t> DrainRxBytes() { return audioBridge_ ? audioBridge_->DrainRxBytes() : std::vector<std::uint8_t>{}; }

    bool TakeModemConnected(int& rxBps) { return audioBridge_ && audioBridge_->TakeModemConnected(rxBps); }
    bool TakeModemEnded() { return audioBridge_ && audioBridge_->TakeModemEnded(); }
    bool HasMedia() const { return audioBridge_ != nullptr; }
    ModemLinkStatus LinkStatus() const { return audioBridge_ ? audioBridge_->GetLinkStatus() : ModemLinkStatus{}; }

private:
    PjsipCallController::Impl& impl_;
    bool callingParty_;
    std::unique_ptr<CallAudioBridge> audioBridge_;
};

class SipAccount : public pj::Account {
public:
    explicit SipAccount(PjsipCallController::Impl& impl) : impl_(impl) {}

    void onRegState(pj::OnRegStateParam& prm) override {
        pj::AccountInfo info = getInfo();
        if (prm.code / 100 == 2) {
            impl_.logger_.Info(std::string("SIP ") + (info.regIsActive ? "registration" : "unregistration") +
                               " result: " + std::to_string(prm.code) + " " + prm.reason);
        } else {
            impl_.logger_.Warn("SIP registration failed: " + std::to_string(prm.code) + " " + prm.reason);
        }
        std::lock_guard<std::mutex> lock(impl_.mutex_);
        impl_.registrationCode_ = prm.code;
        impl_.registrationReason_ = prm.reason;
        if (prm.code / 100 == 2) {
            impl_.registration_ = info.regIsActive ? CallStatus::Registration::Registered
                                                   : CallStatus::Registration::Unregistered;
        } else {
            impl_.registration_ = CallStatus::Registration::Failed;
        }
    }

    void onIncomingCall(pj::OnIncomingCallParam& iprm) override {
        auto call = std::make_unique<SipCall>(*this, impl_, /*callingParty=*/false, iprm.callId);

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
            std::lock_guard<std::mutex> lock(impl_.mutex_);
            ++impl_.rejectedBusy_;
            return; // `call` destructs here; it was never accepted
        }

        std::string remote;
        try {
            remote = RemoteUser(call->getInfo().remoteUri);
        } catch (const pj::Error&) {
        }
        {
            std::lock_guard<std::mutex> lock(impl_.mutex_);
            impl_.remoteParty_ = remote;
            impl_.codec_.clear();
            impl_.audioMediaIndex_ = -1;
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
    : endpoint_(endpoint), config_(std::move(config)), logger_(logger), impl_(std::make_unique<Impl>(logger)) {
    impl_->enableLocalAudio_ = config_.enableLocalAudio;
    impl_->modemOptions_.modulations = config_.modulations;
    impl_->modemOptions_.errorControl = config_.errorControl;
    impl_->modemOptions_.compression = config_.compression;
}

PjsipCallController::~PjsipCallController() {
    if (impl_->account_) {
        try {
            if (impl_->account_->getInfo().regIsActive) {
                impl_->account_->setRegistration(false);
            }
        } catch (const pj::Error& err) {
            logger_.Warn("Unregistration failed during shutdown: " + err.reason);
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
        accountConfig.regConfig.registrarUri = SipUri(config_.server, config_.transport);
        accountConfig.regConfig.timeoutSec = static_cast<unsigned>(config_.registerExpirySeconds);
        accountConfig.regConfig.registerOnAdd = config_.registerOnStart;
        accountConfig.mediaConfig.transportConfig.port = static_cast<unsigned>(config_.rtpPort);
        accountConfig.mediaConfig.transportConfig.portRange = static_cast<unsigned>(config_.rtpPortRange);

        pj::AuthCredInfo cred("digest", config_.realm, config_.extension, 0, config_.password);
        accountConfig.sipConfig.authCreds.push_back(cred);

        account->create(accountConfig);
        impl_->account_ = std::move(account);
        {
            std::lock_guard<std::mutex> lock(impl_->mutex_);
            impl_->registration_ = config_.registerOnStart ? CallStatus::Registration::Registering
                                                           : CallStatus::Registration::Unregistered;
        }
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
        auto call = std::make_unique<SipCall>(*impl_->account_, *impl_, /*callingParty=*/true);
        pj::CallOpParam callParam(true);
        {
            std::lock_guard<std::mutex> lock(impl_->mutex_);
            impl_->remoteParty_ = number;
            impl_->codec_.clear();
            impl_->audioMediaIndex_ = -1;
        }
        call->makeCall(SipUri(number + "@" + config_.server, config_.transport), callParam);
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
    // Modem-level events first. activeCall_ can be retired by PJSIP's
    // thread (RetireCall, under mutex_), so look under the lock -- but
    // hang up outside it: hangup() may call back into RetireCall.
    int rxBps = 0;
    bool connected = false;
    bool ended = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        if (impl_->activeCall_) {
            connected = impl_->activeCall_->TakeModemConnected(rxBps);
            if (!connected) {
                ended = impl_->activeCall_->TakeModemEnded();
            }
        }
    }
    if (connected) {
        CallEvent event{CallEventType::Connected};
        event.connectRateBps = rxBps;
        return event;
    }
    if (ended) {
        logger_.Info("Modem link ended; hanging up the SIP call");
        impl_->swallowNextSipEnded_ = true;
        HangUp();
        return CallEvent{CallEventType::Ended, ResultCode::NoCarrier};
    }

    for (;;) {
        const CallEvent event = impl_->PopEvent();
        if (event.type == CallEventType::Ended && impl_->swallowNextSipEnded_) {
            impl_->swallowNextSipEnded_ = false;
            continue; // already reported when the modem ended
        }
        return event;
    }
}

void PjsipCallController::SendData(const std::vector<std::uint8_t>& bytes) {
    // Matches Dial()/Answer()/HangUp()'s existing convention of reading
    // impl_->activeCall_ without taking mutex_ -- see Impl's own class
    // comment: this is only ever called from the same ModemInstance
    // thread that calls those, never from a pjsua2 callback.
    if (!impl_->activeCall_) {
        return;
    }
    for (std::uint8_t b : bytes) {
        impl_->activeCall_->EnqueueTxByte(b);
    }
}

std::vector<std::uint8_t> PjsipCallController::PollReceivedData() {
    if (!impl_->activeCall_) {
        return {};
    }
    return impl_->activeCall_->DrainRxBytes();
}

CallStatus PjsipCallController::GetStatus() {
    CallStatus status;
    SipCall* call = nullptr;
    int mediaIndex = -1;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        status.registration = impl_->registration_;
        status.registrationCode = impl_->registrationCode_;
        status.registrationReason = impl_->registrationReason_;
        status.rejectedBusy = impl_->rejectedBusy_;
        status.alerting = impl_->pendingIncomingCall_ != nullptr;
        call = impl_->activeCall_.get();
        status.inCall = call != nullptr || status.alerting;
        if (status.inCall) {
            status.remoteParty = impl_->remoteParty_;
            status.codec = impl_->codec_;
        }
        if (call != nullptr) {
            status.mediaActive = call->HasMedia();
            status.modem = call->LinkStatus();
            mediaIndex = impl_->audioMediaIndex_;
        }
    }

    // Outside mutex_: pjsua takes its own locks here, and PJSIP's thread
    // takes mutex_ while holding them (onCallState). `call` stays alive:
    // a retired call is only destroyed by PopEvent(), on this thread.
    if (call != nullptr && mediaIndex >= 0) {
        try {
            const pj::StreamStat stat = call->getStreamStat(static_cast<unsigned>(mediaIndex));
            const pj::RtcpStreamStat& rx = stat.rtcp.rxStat;
            status.jitterMeanMs = rx.jitterUsec.mean / 1000.0;
            status.jitterMaxMs = rx.jitterUsec.max / 1000.0;
            status.rtpPacketsReceived = rx.pkt;
            status.rtpPacketsLost = rx.loss;
            status.rtpBytesReceived = rx.bytes;
            status.rtpBytesSent = stat.rtcp.txStat.bytes;
        } catch (const pj::Error&) {
        }
    }
    return status;
}

bool PjsipCallController::SetRegistered(bool registered) {
    if (!impl_->account_) {
        return true; // no account: nothing is registered either way
    }
    try {
        impl_->account_->setRegistration(registered);
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->registration_ = registered ? CallStatus::Registration::Registering
                                          : CallStatus::Registration::Unregistered;
        return true;
    } catch (const pj::Error& err) {
        if (!registered && err.status == PJ_EINVALIDOP) {
            // Nothing registered (the last REGISTER failed): already done.
            std::lock_guard<std::mutex> lock(impl_->mutex_);
            impl_->registration_ = CallStatus::Registration::Unregistered;
            return true;
        }
        // PJSIP_EBUSY while a REGISTER (or its retry) is in flight.
        logger_.Debug(std::string(registered ? "Registration" : "Unregistration") +
                      " request deferred (" + err.reason + "); retrying");
        return false;
    }
}

} // namespace voipmodem
