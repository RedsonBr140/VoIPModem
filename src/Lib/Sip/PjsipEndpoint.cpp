#include "PjsipEndpoint.h"

#include <pjsua2.hpp> // before Windows headers (winsock2 ordering)

#include <algorithm>

#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

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
        timeEndPeriod(1);
    }
}

bool PjsipEndpoint::Start(const EndpointSettings& settings) {
    const int localPort = settings.localPort;
    try {
        pj::EpConfig epConfig;
        epConfig.logConfig.writer = new LoggerBridge(logger_);
        epConfig.logConfig.level = 5;
        // pjsua only hands a message to logConfig.writer if its level is
        // <= consoleLevel (the writer IS the "console" here), so this
        // must match `level` -- 0 silently dropped every PJSIP message.
        epConfig.logConfig.consoleLevel = 5;
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
        epConfig.medConfig.ecTailLen = settings.echoCanceller ? 128 : 0; // 0 disables echo cancellation (the default: it degrades modem training)

        // Jitter buffer tuned for a modem, not for voice. Found live
        // 2026-11-24 (V.90 answering BinModem): PJSIP's defaults -- no
        // prefetch (jbInit 0) and PROGRESSIVE discard -- ran the receive
        // buffer empty every 1-3 s ("Jitter buffer empty (prefetch=0), plc
        // invoked", "Frame lost, recovered!"). Each such event invents or
        // drops 20 ms of audio, which a V.34 receiver cannot survive: the
        // upstream died within 3 s of data mode at exactly such an event.
        // A modem wants a deep, fixed buffer and never a dropped frame;
        // the added latency only lengthens the round trip, which the
        // modems measure and absorb in training.
        // [Media] JitterBufferMs sets the initial prefetch (default 160);
        // the other depths keep the proportions found live (120/400/1000).
        const int jb = settings.jitterBufferMs;
        epConfig.medConfig.jbInit = jb;
        epConfig.medConfig.jbMinPre = jb * 3 / 4;
        epConfig.medConfig.jbMaxPre = jb * 5 / 2;
        epConfig.medConfig.jbMax = (std::max)(1000, jb * 5);
        epConfig.medConfig.jbDiscardAlgo = PJMEDIA_JB_DISCARD_NONE;

        // No sound card: the null device's clock thread drives the
        // conference bridge, and at Windows' default ~15.6 ms sleep
        // granularity it pulls 20 ms frames in uneven bursts. 1 ms timer
        // resolution for the process keeps that clock steady.
        timeBeginPeriod(1);

        // One transport is created (below), UDP unless [SIP] Transport=TCP.
        // By default PJSIP moves any request of 1300 bytes or more from UDP
        // to TCP (RFC 3261 18.1.1); with no TCP transport that attempt
        // fails ("Unsupported transport", seen on XP) before PJSIP falls
        // back to UDP. Stay on the configured transport.
        pjsip_cfg()->endpt.disable_tcp_switch = PJ_TRUE;

        endpoint_->libInit(epConfig);

        pj::TransportConfig transportConfig;
        transportConfig.port = static_cast<unsigned>(localPort);
        endpoint_->transportCreate(settings.transport == "TCP" ? PJSIP_TRANSPORT_TCP : PJSIP_TRANSPORT_UDP,
                                   transportConfig);

        // No real sound device: Phase 3's audio sink is WAV record/playback
        // (see PjsipCallController's onCallMediaState), which — like the
        // DSP engine that replaces it in Phase 4 — never needs a physical
        // playback/capture device at all. A null device still gives the
        // conference bridge the clock source it needs to run. (An earlier
        // version of this routed to the real local sound device instead;
        // switched after discovering this dev machine has zero capture
        // devices at the Windows API level, which every PJSIP sound-device
        // path requires — see docs/ARCHITECTURE.md "RTP audio (Phase 3)".)
        endpoint_->audDevManager().setNullDev();

        endpoint_->libStart();
        RestrictCodecsToG711(settings.preferredCodec);
        started_ = true;
        logger_.Info("PJSIP endpoint started, " + settings.transport + " port " + std::to_string(localPort) +
                     ", codec " + settings.preferredCodec + " first, jitter buffer " + std::to_string(jb) + " ms" +
                     (settings.echoCanceller ? ", echo canceller ON" : ""));
        return true;
    } catch (const pj::Error& err) {
        logger_.Error("Failed to start PJSIP endpoint: " + err.info());
        return false;
    }
}

void PjsipEndpoint::RestrictCodecsToG711(const std::string& preferred) {
    for (const pj::CodecInfo& codec : endpoint_->codecEnum2()) {
        const bool isG711 = codec.codecId.rfind("PCMU/", 0) == 0 || codec.codecId.rfind("PCMA/", 0) == 0;
        try {
            const bool first = codec.codecId.rfind(preferred + "/", 0) == 0;
            endpoint_->codecSetPriority(codec.codecId, isG711 ? static_cast<pj_uint8_t>(first ? 220 : 210) : 0);
        } catch (const pj::Error& err) {
            logger_.Warn("Failed to set priority for codec '" + codec.codecId + "': " + err.info());
        }
        if (isG711) {
            // No packet loss concealment: a missing frame must stay
            // silence, not synthetic audio a modem receiver would try
            // to demodulate (see the jitter buffer note in Start()).
            try {
                pj::CodecParam param = endpoint_->codecGetParam(codec.codecId);
                param.setting.plc = false;
                endpoint_->codecSetParam(codec.codecId, param);
            } catch (const pj::Error& err) {
                logger_.Warn("Failed to disable PLC for codec '" + codec.codecId + "': " + err.info());
            }
        }
    }
}

void PjsipEndpoint::RegisterCurrentThread(const std::string& threadName) {
    if (!endpoint_->libIsThreadRegistered()) {
        endpoint_->libRegisterThread(threadName);
    }
}

std::string PjsipEndpoint::LibraryVersion() {
    try {
        return endpoint_->libVersion().full;
    } catch (const pj::Error&) {
        return "";
    }
}

pj::Endpoint& PjsipEndpoint::Raw() {
    return *endpoint_;
}

} // namespace voipmodem
