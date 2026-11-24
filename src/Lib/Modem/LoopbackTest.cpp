#include "LoopbackTest.h"

#include <cctype>
#include <cstdint>
#include <vector>

extern "C" {
#include <v90modem.h>
}

namespace voipmodem {

namespace {

int Mark(void*) { return 1; }
void Drop(void*, int) {}

unsigned ParseList(const std::string& list) {
    unsigned mask = 0;
    std::string item;
    for (size_t i = 0; i <= list.size(); ++i) {
        if (i == list.size() || list[i] == ',') {
            if (item == "V21") mask |= V90M_MOD_V21;
            else if (item == "V22" || item == "V22BIS") mask |= V90M_MOD_V22;
            else if (item == "V32" || item == "V32BIS") mask |= V90M_MOD_V32BIS;
            else if (item == "V34") mask |= V90M_MOD_V34;
            item.clear();
        } else if (!std::isspace(static_cast<unsigned char>(list[i]))) {
            item += static_cast<char>(std::toupper(static_cast<unsigned char>(list[i])));
        }
    }
    return mask;
}

const char* Describe(v90m_conn c) {
    switch (c) {
        case V90M_CONN_V21: return "V.21";
        case V90M_CONN_V22: return "V.22";
        case V90M_CONN_V22BIS: return "V.22bis";
        case V90M_CONN_V32BIS: return "V.32bis";
        case V90M_CONN_V34: return "V.34";
        default: return "other";
    }
}

} // namespace

LoopbackResult RunModemLoopback(const std::string& modulations, double maxLineSeconds) {
    LoopbackResult result;
    unsigned mask = ParseList(modulations);
    if (mask == 0) {
        // V.90/V.92 need a digital (PCM) answerer and an analog caller on a
        // real G.711 path; back to back, the analog modes are what can be tested.
        mask = V90M_MOD_V21 | V90M_MOD_V22 | V90M_MOD_V32BIS | V90M_MOD_V34;
    }

    v90m_cfg answerCfg;
    v90m_cfg_default(&answerCfg);
    answerCfg.role = V90M_ROLE_ANSWER;
    answerCfg.modulations = mask;
    answerCfg.getbit = Mark;
    answerCfg.putbit = Drop;
    v90m_cfg callCfg = answerCfg;
    callCfg.role = V90M_ROLE_ORIGINATE;

    int err = 0;
    v90m_t* answerer = v90m_create(&answerCfg, &err);
    v90m_t* caller = v90m_create(&callCfg, &err);
    if (answerer == nullptr || caller == nullptr) {
        v90m_destroy(answerer);
        v90m_destroy(caller);
        result.failure = "libv90modem could not create the modems (error " + std::to_string(err) + ")";
        return result;
    }

    constexpr size_t kBlock = 80; // 10 ms
    std::vector<std::int16_t> toCaller(kBlock, 0), toAnswerer(kBlock, 0), callerTx(kBlock), answererTx(kBlock);
    const int blocks = static_cast<int>(maxLineSeconds * 100.0);
    int b = 0;
    for (; b < blocks; ++b) {
        v90m_process_linear(caller, toCaller.data(), callerTx.data(), kBlock);
        v90m_process_linear(answerer, toAnswerer.data(), answererTx.data(), kBlock);
        toAnswerer = callerTx;
        toCaller = answererTx;

        v90m_state cs, as;
        v90m_get_state(caller, &cs);
        v90m_get_state(answerer, &as);
        if (cs.connected && as.connected) {
            v90m_stats stats;
            v90m_get_stats(caller, &stats);
            result.connected = true;
            result.modulation = Describe(cs.modulation);
            result.txBps = cs.tx_bps;
            result.rxBps = cs.rx_bps;
            result.snrDb = stats.snr_db10 >= 0 ? stats.snr_db10 / 10.0 : -1;
            break;
        }
        if (cs.end != V90M_END_NONE || as.end != V90M_END_NONE) {
            result.failure = "a modem gave up (caller end " + std::to_string(static_cast<int>(cs.end)) +
                             ", answerer end " + std::to_string(static_cast<int>(as.end)) + ")";
            break;
        }
    }
    result.simulatedSeconds = (b + 1) / 100.0;
    if (!result.connected && result.failure.empty()) {
        result.failure = "no connection within " + std::to_string(static_cast<int>(maxLineSeconds)) + " s of line time";
    }
    v90m_destroy(answerer);
    v90m_destroy(caller);
    return result;
}

std::string ModemEngineVersion() {
    return v90m_version();
}

} // namespace voipmodem
