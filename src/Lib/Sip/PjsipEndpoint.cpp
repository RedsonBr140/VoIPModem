#include "PjsipEndpoint.h"

#include <pjsua2.hpp>

namespace voipmodem {

namespace {

// Routes PJSIP's own internal logging (SIP dialogs, transport events,
// retransmissions, auth failures, ...) into our Logger instead of
// wherever PJSIP would otherwise print it, per CLAUDE.md's "structured
// logging for... SIP dialogs" rule. Ownership: pjsua2's LogConfig takes
// this by raw pointer and destroys it itself when the Endpoint is
// destroyed (see LogConfig::writer's doc comment in pjsua2/endpoint.hpp),
// so this is deliberately heap-allocated and never deleted by us.
class LoggerBridge : public pj::LogWriter {
public:
    explicit LoggerBridge(Logger& logger) : logger_(logger) {}

    void write(const pj::LogEntry& entry) override {
        // PJSIP's own verbosity levels run 0 (fatal) to 6 (trace); map
        // the ones we actually want surfaced onto our four levels.
        if (entry.level <= 1) {
            logger_.Error("[SIP] " + entry.msg);
        } else if (entry.level <= 2) {
            logger_.Warn("[SIP] " + entry.msg);
        } else if (entry.level <= 3) {
            logger_.Info("[SIP] " + entry.msg);
        } else {
            logger_.Debug("[SIP] " + entry.msg);
        }
    }

private:
    Logger& logger_;
};

} // namespace

PjsipEndpoint::PjsipEndpoint(Logger& logger) : logger_(logger), endpoint_(std::make_unique<pj::Endpoint>()) {
    endpoint_->libCreate();
}

PjsipEndpoint::~PjsipEndpoint() {
    if (started_) {
        try {
            endpoint_->libDestroy();
        } catch (const pj::Error& err) {
            logger_.Error("PJSIP libDestroy() failed: " + err.info());
        }
    }
}

bool PjsipEndpoint::Start(int localPort) {
    try {
        pj::EpConfig epConfig;
        epConfig.logConfig.writer = new LoggerBridge(logger_);
        epConfig.logConfig.level = 5;
        epConfig.logConfig.consoleLevel = 0; // we route everything through LoggerBridge instead
        epConfig.uaConfig.maxCalls = 32;      // generous headroom for several ModemInstance lines

        // Match the spec's G.711-at-8kHz target end to end (no resampling
        // anywhere in the chain) and disable the voice-oriented processing
        // that would otherwise corrupt modem tones — see
        // docs/PROJECT_SPEC.md section 4.D ("Disable or avoid audio
        // processing that corrupts modem tones") and
        // docs/ARCHITECTURE.md "RTP audio (Phase 3)".
        epConfig.medConfig.clockRate = 8000;
        epConfig.medConfig.sndClockRate = 8000;
        epConfig.medConfig.noVad = true;  // no silence suppression — a steady carrier isn't "silence"
        epConfig.medConfig.ecTailLen = 0; // 0 disables echo cancellation

        endpoint_->libInit(epConfig);

        pj::TransportConfig transportConfig;
        transportConfig.port = static_cast<unsigned>(localPort);
        endpoint_->transportCreate(PJSIP_TRANSPORT_UDP, transportConfig);

        // Phase 3: route call audio to this machine's sound device (see
        // PjsipCallController's onCallMediaState) so a connected call is
        // actually audible, as a byear-verifiable proof the RTP/codec path
        // is clean. This opens the default capture/playback devices for
        // the process's lifetime — expected and temporary: Phase 4
        // replaces this sink with the DSP engine, at which point nothing
        // needs a real sound device at all.
        endpoint_->libStart();
        RestrictCodecsToG711();
        started_ = true;
        logger_.Info("PJSIP endpoint started, UDP port " + std::to_string(localPort));
        return true;
    } catch (const pj::Error& err) {
        logger_.Error("Failed to start PJSIP endpoint: " + err.info());
        return false;
    }
}

void PjsipEndpoint::RestrictCodecsToG711() {
    for (const pj::CodecInfo& codec : endpoint_->codecEnum2()) {
        const bool isG711 = codec.codecId.rfind("PCMU/", 0) == 0 || codec.codecId.rfind("PCMA/", 0) == 0;
        try {
            endpoint_->codecSetPriority(codec.codecId, isG711 ? static_cast<pj_uint8_t>(210) : 0);
        } catch (const pj::Error& err) {
            logger_.Warn("Failed to set priority for codec '" + codec.codecId + "': " + err.info());
        }
    }
}

void PjsipEndpoint::RegisterCurrentThread(const std::string& threadName) {
    if (!endpoint_->libIsThreadRegistered()) {
        endpoint_->libRegisterThread(threadName);
    }
}

pj::Endpoint& PjsipEndpoint::Raw() {
    return *endpoint_;
}

} // namespace voipmodem
