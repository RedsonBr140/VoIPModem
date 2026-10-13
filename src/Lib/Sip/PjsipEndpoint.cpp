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
        endpoint_->libInit(epConfig);

        pj::TransportConfig transportConfig;
        transportConfig.port = static_cast<unsigned>(localPort);
        endpoint_->transportCreate(PJSIP_TRANSPORT_UDP, transportConfig);

        // No audio device yet — Phase 2 is signaling only. Phase 3 wires
        // up real RTP/codec handling; grabbing a host sound device before
        // then would be premature per CLAUDE.md's "never treat success at
        // one layer as proof of success at another" boundary.
        endpoint_->audDevManager().setNullDev();

        endpoint_->libStart();
        started_ = true;
        logger_.Info("PJSIP endpoint started, UDP port " + std::to_string(localPort));
        return true;
    } catch (const pj::Error& err) {
        logger_.Error("Failed to start PJSIP endpoint: " + err.info());
        return false;
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
