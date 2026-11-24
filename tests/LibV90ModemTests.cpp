// Checks that libv90modem, as built by our MSVC project
// (third_party/libv90modem-build), actually works: two instances, a
// caller and an answerer, connected back to back over a lossless
// in-memory 4-wire line through the public API -- the same shape as
// upstream's examples/call_loop.c. Upstream validates the library under
// gcc/clang/mingw; this is the one check that MSVC's build of it is sane
// before any real call depends on it.
#include "TestFramework.h"

#include <cstdint>
#include <vector>

extern "C" {
#include <v90modem.h>
}

namespace {

int Mark(void*) { return 1; }
void Drop(void*, int) {}

struct LoopResult {
    bool callerConnected = false;
    bool answererConnected = false;
    v90m_conn callerMode = V90M_CONN_NONE;
    v90m_conn answererMode = V90M_CONN_NONE;
};

LoopResult RunBackToBack(unsigned modulations, double maxSeconds) {
    v90m_cfg answerCfg;
    v90m_cfg_default(&answerCfg);
    answerCfg.role = V90M_ROLE_ANSWER;
    answerCfg.modulations = modulations;
    answerCfg.getbit = Mark;
    answerCfg.putbit = Drop;

    v90m_cfg callCfg = answerCfg;
    callCfg.role = V90M_ROLE_ORIGINATE;

    int err = 0;
    v90m_t* answerer = v90m_create(&answerCfg, &err);
    v90m_t* caller = v90m_create(&callCfg, &err);
    LoopResult result;
    if (answerer == nullptr || caller == nullptr) {
        v90m_destroy(answerer);
        v90m_destroy(caller);
        return result;
    }

    constexpr size_t kBlock = 80; // 10 ms
    std::vector<int16_t> toCaller(kBlock, 0), toAnswerer(kBlock, 0), callerTx(kBlock), answererTx(kBlock);
    const int blocks = static_cast<int>(maxSeconds * 100.0);
    for (int b = 0; b < blocks; ++b) {
        v90m_process_linear(caller, toCaller.data(), callerTx.data(), kBlock);
        v90m_process_linear(answerer, toAnswerer.data(), answererTx.data(), kBlock);
        toAnswerer = callerTx;
        toCaller = answererTx;

        v90m_state cs, as;
        v90m_get_state(caller, &cs);
        v90m_get_state(answerer, &as);
        if (cs.connected && as.connected) {
            result.callerConnected = result.answererConnected = true;
            result.callerMode = cs.modulation;
            result.answererMode = as.modulation;
            break;
        }
        if (cs.end != V90M_END_NONE || as.end != V90M_END_NONE) {
            break;
        }
    }
    v90m_destroy(answerer);
    v90m_destroy(caller);
    return result;
}

} // namespace

TEST(LibV90Modem_MsvcBuild_V21BackToBackConnects) {
    const LoopResult r = RunBackToBack(V90M_MOD_V21, 20.0);
    EXPECT_TRUE(r.callerConnected);
    EXPECT_TRUE(r.answererConnected);
    EXPECT_EQ(static_cast<int>(r.callerMode), static_cast<int>(V90M_CONN_V21));
}

TEST(LibV90Modem_MsvcBuild_V34BackToBackConnects) {
    const LoopResult r = RunBackToBack(V90M_MOD_V34, 40.0);
    EXPECT_TRUE(r.callerConnected);
    EXPECT_TRUE(r.answererConnected);
    EXPECT_EQ(static_cast<int>(r.callerMode), static_cast<int>(V90M_CONN_V34));
}
