#include "CallAudioBridge.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

extern "C" {
#include <v90modem.h>
}

namespace voipmodem {

namespace {

constexpr int kBitsPerSample = 16;
constexpr int kFrameTimeUsec = 20000; // 20ms -> 160 samples at 8kHz
constexpr int kSampleRateHz = 8000;

// The C callbacks libv90modem calls from inside v90m_process_*; all on
// pjsua2's media thread (see CallAudioBridge's class comment).
int GetBitCallback(void* user) { return static_cast<CallAudioBridge*>(user)->ModemGetBit(); }
void PutBitCallback(void* user, int bit) { static_cast<CallAudioBridge*>(user)->ModemPutBit(bit); }
void EventCallback(void* user, v90m_event ev) { static_cast<CallAudioBridge*>(user)->OnModemEvent(static_cast<int>(ev)); }

const char* DescribeConnection(v90m_conn c) {
    switch (c) {
        case V90M_CONN_V21: return "V.21";
        case V90M_CONN_V22: return "V.22";
        case V90M_CONN_V22BIS: return "V.22bis";
        case V90M_CONN_V90: return "V.90";
        case V90M_CONN_V34: return "V.34";
        case V90M_CONN_V32BIS: return "V.32bis";
        case V90M_CONN_V92: return "V.92";
        case V90M_CONN_BELL103: return "Bell 103";
        case V90M_CONN_BELL212A: return "Bell 212A";
        case V90M_CONN_X2: return "x2";
        case V90M_CONN_K56FLEX: return "K56flex";
        default: return "none";
    }
}

const char* DescribeEnd(v90m_end e) {
    switch (e) {
        case V90M_END_NONE: return "none";
        case V90M_END_FAILED: return "start-up failed";
        case V90M_END_LOST: return "connection lost";
        case V90M_END_CLEARDOWN_FAR: return "far modem cleared down";
        case V90M_END_CLEARDOWN_OURS: return "we cleared down";
        case V90M_END_NO_ANSWER_TONE: return "no answer tone";
        case V90M_END_NO_JM: return "ANSam heard but no JM answered our CM";
        case V90M_END_NO_COMMON: return "V.8 found no modulation in common";
        case V90M_END_RETRAIN_FAILED: return "retrain failed";
        case V90M_END_X2_FAILED: return "x2 start-up failed";
        case V90M_END_K56_FAILED: return "K56flex start-up failed";
        default: return "unknown";
    }
}

const char* DescribeEcState(int s) {
    switch (s) {
        case V90M_EC_DETECT: return "detection phase";
        case V90M_EC_ESTABLISH: return "establishing LAPM";
        case V90M_EC_CONNECTED: return "V.42 LAPM connected";
        case V90M_EC_ASYNC: return "no error control (start-stop fallback)";
        case V90M_EC_RELEASING: return "releasing";
        case V90M_EC_DISCONNECTED: return "disconnected";
        default: return "unknown";
    }
}

const char* DescribeCompression(int alg) {
    switch (alg) {
        case V90M_EC_COMP_V42BIS: return "V.42bis";
        case V90M_EC_COMP_V44: return "V.44";
        default: return "none";
    }
}

// Parses ModemInstanceConfig::modulations ("V21,V34,..."); 0 if empty or
// nothing recognised. Unknown names are reported through `unknown`.
unsigned ParseModulations(const std::string& list, std::string& unknown) {
    unsigned mask = 0;
    size_t start = 0;
    while (start <= list.size()) {
        const size_t comma = list.find(',', start);
        std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        for (auto& ch : item) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        if (item == "V21") mask |= V90M_MOD_V21;
        else if (item == "V22" || item == "V22BIS") mask |= V90M_MOD_V22;
        else if (item == "V32" || item == "V32BIS") mask |= V90M_MOD_V32BIS;
        else if (item == "V34") mask |= V90M_MOD_V34;
        else if (item == "V90") mask |= V90M_MOD_V90;
        else if (item == "V92") mask |= V90M_MOD_V92 | V90M_MOD_V90; // V.92 requires V.90
        else if (item == "BELL103") mask |= V90M_MOD_BELL103;
        else if (item == "BELL212A") mask |= V90M_MOD_BELL212A | V90M_MOD_V22;
        else if (!item.empty()) unknown += (unknown.empty() ? "" : ",") + item;
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return mask;
}

} // namespace

CallAudioBridge::CallAudioBridge(bool callingParty, TaggedLogger& logger, bool enableV34Probing, bool enableLocalAudio,
                                 bool alaw, const std::string& modulations)
    : callingParty_(callingParty), logger_(logger) {
    (void)enableV34Probing; // see header

    v90m_cfg cfg;
    v90m_cfg_default(&cfg);
    cfg.role = callingParty ? V90M_ROLE_ORIGINATE : V90M_ROLE_ANSWER;
    std::string unknown;
    const unsigned configured = ParseModulations(modulations, unknown);
    if (!unknown.empty()) {
        logger_.Warn("Modulations: ignoring unknown name(s) " + unknown);
    }
    if (configured != 0) {
        cfg.modulations = configured;
    } else {
        cfg.modulations |= V90M_MOD_V92; // defaults + V.92; falls back to V.90 / lower by itself
    }
    modulationsDescription_ = modulations.empty() ? "defaults+V92" : modulations;
    cfg.alaw = alaw ? 1 : 0;
    cfg.getbit = GetBitCallback;
    cfg.putbit = PutBitCallback;
    cfg.on_event = EventCallback;
    cfg.user = this;

    int err = 0;
    modem_ = v90m_create(&cfg, &err);
    if (modem_ == nullptr) {
        logger_.Error("libv90modem: v90m_create failed, error " + std::to_string(err));
    }

    v90m_ec_cfg ecCfg;
    v90m_ec_cfg_default(&ecCfg, callingParty ? 1 : 0);
    ec_ = v90m_ec_create(&ecCfg, &err);
    if (ec_ == nullptr) {
        logger_.Error("libv90modem: v90m_ec_create failed, error " + std::to_string(err));
    }

    if (enableLocalAudio) {
        localSpeaker_.emplace();
    }
}

CallAudioBridge::~CallAudioBridge() {
    // Detach from the conference bridge first (asynchronous), then take
    // the modem away under frameMutex_ so a frame callback already in
    // flight finishes before it's freed, and any later one sees nullptr.
    // pj::AudioMediaPort's own destructor then stops callbacks for good;
    // the window between our members' destruction and that is
    // pre-existing pjsua2 behaviour, not new to this class.
    try {
        unregisterMediaPort();
    } catch (const pj::Error&) {
    }
    std::lock_guard<std::mutex> lock(frameMutex_);
    v90m_ec_destroy(ec_);
    ec_ = nullptr;
    ecActive_ = false;
    v90m_destroy(modem_);
    modem_ = nullptr;
}

void CallAudioBridge::Start(pj::AudioMedia& callAudio) {
    pj::MediaFormatAudio format;
    format.init(PJMEDIA_FORMAT_PCM, kSampleRateHz, 1, kFrameTimeUsec, kBitsPerSample);
    createPort("voipmodem-libv90modem", format);

    callAudio.startTransmit(*this);
    startTransmit(callAudio);

    logger_.Info(std::string("CallAudioBridge started (") + (callingParty_ ? "calling" : "answering") +
                 "); libv90modem " + v90m_version() +
                 " offering " + modulationsDescription_ + " with V.42" +
                 (localSpeaker_.has_value() ? "; local audio monitor enabled" : ""));
}

int CallAudioBridge::ModemGetBit() {
    return ecActive_ ? v90m_ec_tx_bit(ec_) : 1; // mark until connected
}

void CallAudioBridge::ModemPutBit(int bit) {
    if (ecActive_) {
        v90m_ec_rx_bit(ec_, bit);
    }
}

void CallAudioBridge::OnModemEvent(int event) {
    v90m_state st;
    v90m_get_state(modem_, &st);

    switch (static_cast<v90m_event>(event)) {
        case V90M_EV_CONNECTED: {
            v90m_stats stats;
            v90m_get_stats(modem_, &stats);
            logger_.Info(std::string("libv90modem: CONNECTED ") + DescribeConnection(st.modulation) + " tx " +
                         std::to_string(st.tx_bps) + " / rx " + std::to_string(st.rx_bps) + " bit/s" +
                         (st.used_v8 ? " (via V.8)" : " (no V.8)") + ", SNR " +
                         (stats.snr_db10 >= 0 ? std::to_string(stats.snr_db10 / 10.0) + " dB" : "n/a") +
                         ", round trip " + std::to_string(stats.rtd) + " samples");
            v90m_ec_cfg ecCfg;
            v90m_ec_cfg_default(&ecCfg, callingParty_ ? 1 : 0);
            // Deliberately NOT honouring st.odp_bypass (libv90modem's docs
            // suggest detect = 0 then). With detection on, the answerer
            // still starts LAPM on a caller's flags/SABME without an ODP
            // (v42.c, "with or without ODP"), so it serves both kinds of
            // caller; with it off it serves only one. Found live
            // 2026-11-24: BinModem calling, V.90 with both ends V.92 and
            // LAPM in V.8 -> odp_bypass 1, but BinModem ran the detection
            // phase, never got our ADP, and LAPM never started.
            if (!callingParty_ && st.odp_bypass) {
                logger_.Info("libv90modem: caller may skip V.42 detection (V.92 9.3.1); keeping detection on");
            }
            if (ec_ != nullptr && v90m_ec_reset(ec_, &ecCfg) == V90M_OK) {
                ecActive_ = true;
                lastEcState_ = -1;
            } else {
                logger_.Error("libv90modem: V.42 endpoint reset failed; no data will flow");
            }
            std::lock_guard<std::mutex> lock(dataMutex_);
            connectPending_ = true;
            connectRxBps_ = st.rx_bps;
            break;
        }
        case V90M_EV_RATES:
            logger_.Info(std::string("libv90modem: rates now ") + DescribeConnection(st.modulation) + " tx " +
                         std::to_string(st.tx_bps) + " / rx " + std::to_string(st.rx_bps));
            break;
        case V90M_EV_DATA_STOPPED:
            logger_.Info("libv90modem: data stopped (retrain or renegotiation)");
            if (ecActive_) v90m_ec_line_stopped(ec_);
            break;
        case V90M_EV_DATA_MOVING:
            logger_.Info("libv90modem: data moving");
            if (ecActive_) v90m_ec_line_restored(ec_);
            break;
        case V90M_EV_ENDED: {
            logger_.Info(std::string("libv90modem: modem ended -- ") + DescribeEnd(st.end));
            if (ecActive_) {
                v90m_ec_abort(ec_);
                ecActive_ = false;
            }
            std::lock_guard<std::mutex> lock(dataMutex_);
            if (!endSignalled_) { endPending_ = true; endSignalled_ = true; } // once per call
            break;
        }
        default:
            break;
    }
}

void CallAudioBridge::LogErrorControlState() {
    const int state = v90m_ec_state(ec_);
    if (state == lastEcState_) {
        return;
    }
    lastEcState_ = state;
    std::string message = std::string("libv90modem: V.42 ") + DescribeEcState(state);
    if (state == V90M_EC_CONNECTED || state == V90M_EC_DISCONNECTED) {
        v90m_ec_status status;
        v90m_ec_get_status(ec_, &status);
        if (state == V90M_EC_CONNECTED) {
            message += std::string(", compression ") + DescribeCompression(status.comp_alg);
        } else {
            message += ", reason " + std::to_string(status.reason);
        }
    }
    logger_.Info(message);
}

void CallAudioBridge::PumpErrorControl(unsigned elapsedMs) {
    if (!ecActive_) {
        return;
    }
    v90m_ec_tick(ec_, elapsedMs);

    // V.42 gave up for good (no SABME, link failure, peer DISC...) while
    // the modem is still connected: no data can flow any more, so end
    // the call rather than hold a dead line open.
    if (v90m_ec_state(ec_) == V90M_EC_DISCONNECTED) {
        LogErrorControlState();
        logger_.Info("libv90modem: V.42 link is gone; clearing down");
        ecActive_ = false;
        v90m_cleardown(modem_);
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (!endSignalled_) { endPending_ = true; endSignalled_ = true; } // once per call
        return;
    }

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        size_t space = v90m_ec_write_space(ec_);
        std::uint8_t chunk[256];
        while (space > 0 && !txByteQueue_.empty()) {
            size_t n = 0;
            while (n < sizeof chunk && n < space && !txByteQueue_.empty()) {
                chunk[n++] = txByteQueue_.front();
                txByteQueue_.pop_front();
            }
            const size_t accepted = v90m_ec_write(ec_, chunk, n);
            for (size_t i = n; i > accepted; --i) {
                txByteQueue_.push_front(chunk[i - 1]); // give back what didn't fit
            }
            if (accepted < n) break;
            space = v90m_ec_write_space(ec_);
        }

        std::uint8_t buf[256];
        size_t got;
        while ((got = v90m_ec_read(ec_, buf, sizeof buf)) > 0) {
            rxByteQueue_.insert(rxByteQueue_.end(), buf, buf + got);
        }
    }

    LogErrorControlState();
}

void CallAudioBridge::onFrameRequested(pj::MediaFrame& frame) {
    const unsigned sampleCount = frame.size / sizeof(std::int16_t);
    frame.buf.resize(frame.size);
    frame.type = PJMEDIA_FRAME_TYPE_AUDIO;
    for (unsigned i = 0; i < sampleCount; ++i) {
        const std::int16_t pcm = (i < pendingTxSamples_.size()) ? pendingTxSamples_[i] : 0;
        frame.buf[2 * i] = static_cast<unsigned char>(pcm & 0xFF);
        frame.buf[2 * i + 1] = static_cast<unsigned char>((pcm >> 8) & 0xFF);
    }
}

void CallAudioBridge::onFrameReceived(pj::MediaFrame& frame) {
    const size_t sampleCount = frame.buf.size() / sizeof(std::int16_t);
    std::lock_guard<std::mutex> frameLock(frameMutex_);
    if (sampleCount == 0 || modem_ == nullptr) {
        return;
    }

    std::vector<std::int16_t> rx(sampleCount);
    for (size_t i = 0; i < sampleCount; ++i) {
        rx[i] = static_cast<std::int16_t>(frame.buf[2 * i] | (frame.buf[2 * i + 1] << 8));
    }

    pendingTxSamples_.assign(sampleCount, 0);
    v90m_process_linear(modem_, rx.data(), pendingTxSamples_.data(), sampleCount);

    PumpErrorControl(static_cast<unsigned>(sampleCount * 1000 / kSampleRateHz));
    PlayLocally(rx, pendingTxSamples_);
}

void CallAudioBridge::PlayLocally(const std::vector<std::int16_t>& rx, const std::vector<std::int16_t>& tx) {
    if (!localSpeaker_.has_value() || !localSpeaker_->IsAvailable()) {
        return;
    }
    std::vector<dsp::Sample> mixed(std::max(rx.size(), tx.size()), 0.0f);
    for (size_t i = 0; i < rx.size(); ++i) mixed[i] += rx[i] / 32768.0f;
    for (size_t i = 0; i < tx.size(); ++i) mixed[i] += tx[i] / 32768.0f;
    localSpeaker_->Write(mixed);
}

void CallAudioBridge::EnqueueTxByte(std::uint8_t byte) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    txByteQueue_.push_back(byte);
}

std::vector<std::uint8_t> CallAudioBridge::DrainRxBytes() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    std::vector<std::uint8_t> result(rxByteQueue_.begin(), rxByteQueue_.end());
    rxByteQueue_.clear();
    return result;
}

bool CallAudioBridge::TakeModemConnected(int& rxBps) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (!connectPending_) {
        return false;
    }
    connectPending_ = false;
    rxBps = connectRxBps_;
    return true;
}

bool CallAudioBridge::TakeModemEnded() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (!endPending_) {
        return false;
    }
    endPending_ = false;
    return true;
}

} // namespace voipmodem
