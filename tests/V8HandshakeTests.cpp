#include "../src/Lib/Dsp/AnsamTone.h"
#include "../src/Lib/Dsp/V8Handshake.h"

#include <cmath>
#include "../src/Lib/Logging/Logger.h"
#include "../src/Lib/Logging/TaggedLogger.h"
#include "TestFramework.h"

using namespace voipmodem::dsp;
using voipmodem::Logger;
using voipmodem::TaggedLogger;

namespace {

// Runs both sides of a V.8 handshake against each other, each side's TX
// samples fed directly as the other side's RX for the same time slice --
// a perfect, lossless loopback (no real audio path, no noise). Not a
// substitute for a live call, but the right level to prove the state
// machine itself -- timers, preamble search, menu matching, CJ -- is
// internally consistent end to end before ever touching real audio.
// Returns false if neither side reached Negotiated within `maxFrames`.
bool RunLoopbackToCompletion(V8Handshake& callDce, V8Handshake& answerDce, int frameSampleCount, int maxFrames) {
    std::vector<Sample> callTx;
    std::vector<Sample> answerTx;
    std::vector<Sample> silence(static_cast<size_t>(frameSampleCount), 0.0f);

    // First frame: neither side has anything from the other yet.
    callDce.ProcessFrame(silence, callTx, frameSampleCount);
    answerDce.ProcessFrame(silence, answerTx, frameSampleCount);

    for (int frame = 0; frame < maxFrames; ++frame) {
        if (callDce.GetOutcome() == V8HandshakeOutcome::Negotiated &&
            answerDce.GetOutcome() == V8HandshakeOutcome::Negotiated) {
            return true;
        }

        const std::vector<Sample> rxForCallDce = answerTx;
        const std::vector<Sample> rxForAnswerDce = callTx;

        callDce.ProcessFrame(rxForCallDce, callTx, frameSampleCount);
        answerDce.ProcessFrame(rxForAnswerDce, answerTx, frameSampleCount);
    }

    return callDce.GetOutcome() == V8HandshakeOutcome::Negotiated &&
           answerDce.GetOutcome() == V8HandshakeOutcome::Negotiated;
}

V8CallMenu MakeDataMenu(bool withV34, bool withV21) {
    V8CallMenu menu;
    menu.callFunction = V8CallFunction::Data;
    V8ModulationModes modes;
    modes.v34Duplex = withV34;
    modes.v21 = withV21;
    menu.modulationModes = modes;
    return menu;
}

} // namespace

TEST(V8Handshake_FullLoopback_BothSidesNegotiate) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceTest");
    TaggedLogger answerLogger(logger, "AnswerDceTest");

    V8Handshake callDce(V8Handshake::Role::CallDce, MakeDataMenu(true, true), callLogger);
    V8Handshake answerDce(V8Handshake::Role::AnswerDce, MakeDataMenu(true, true), answerLogger);

    // 20ms/160-sample frames, same granularity as real call audio;
    // generous frame budget (the state machine's own timers add up to a
    // couple of real seconds -- 0.2s + 0.5s plus several CM/JM repeats).
    const bool reachedNegotiated = RunLoopbackToCompletion(callDce, answerDce, 160, 2000);

    EXPECT_TRUE(reachedNegotiated);
    EXPECT_EQ(static_cast<int>(callDce.GetOutcome()), static_cast<int>(V8HandshakeOutcome::Negotiated));
    EXPECT_EQ(static_cast<int>(answerDce.GetOutcome()), static_cast<int>(V8HandshakeOutcome::Negotiated));
}

TEST(V8Handshake_FullLoopback_EachSideLearnsTheOthersMenu) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceTest");
    TaggedLogger answerLogger(logger, "AnswerDceTest");

    V8Handshake callDce(V8Handshake::Role::CallDce, MakeDataMenu(true, true), callLogger);
    V8Handshake answerDce(V8Handshake::Role::AnswerDce, MakeDataMenu(true, true), answerLogger);

    const bool reachedNegotiated = RunLoopbackToCompletion(callDce, answerDce, 160, 2000);
    EXPECT_TRUE(reachedNegotiated);
    if (!reachedNegotiated) {
        return; // avoid dereferencing empty optionals below on a failed run
    }

    // Call DCE's remote menu is what JM said -- the answer DCE's
    // intersection of CM's modes with its own, which here is identical
    // to what the call DCE originally offered (both sides offer the same
    // modes in this test).
    EXPECT_TRUE(callDce.GetRemoteMenu().has_value());
    EXPECT_TRUE(callDce.GetRemoteMenu().value().modulationModes.has_value());
    EXPECT_TRUE(callDce.GetRemoteMenu().value().modulationModes.value().v34Duplex);
    EXPECT_TRUE(callDce.GetRemoteMenu().value().modulationModes.value().v21);

    // Answer DCE's remote menu is CM's own content, verbatim.
    EXPECT_TRUE(answerDce.GetRemoteMenu().has_value());
    EXPECT_TRUE(answerDce.GetRemoteMenu().value().modulationModes.has_value());
    EXPECT_TRUE(answerDce.GetRemoteMenu().value().modulationModes.value().v34Duplex);

    const auto selected = SelectModulationMode(callDce.GetRemoteMenu().value().modulationModes.value());
    EXPECT_TRUE(selected.has_value());
    if (selected.has_value()) {
        EXPECT_EQ(static_cast<int>(selected.value()), static_cast<int>(V8ModulationModeItem::V34Duplex));
    }
}

TEST(V8Handshake_MismatchedModes_JmReflectsOnlyTheIntersection) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceTest");
    TaggedLogger answerLogger(logger, "AnswerDceTest");

    // Call DCE offers V.34 and V.21; answer DCE only has V.21.
    V8Handshake callDce(V8Handshake::Role::CallDce, MakeDataMenu(true, true), callLogger);
    V8Handshake answerDce(V8Handshake::Role::AnswerDce, MakeDataMenu(false, true), answerLogger);

    const bool reachedNegotiated = RunLoopbackToCompletion(callDce, answerDce, 160, 2000);
    EXPECT_TRUE(reachedNegotiated);
    if (!reachedNegotiated || !callDce.GetRemoteMenu().has_value() ||
        !callDce.GetRemoteMenu().value().modulationModes.has_value()) {
        EXPECT_TRUE(callDce.GetRemoteMenu().has_value());
        return;
    }

    const V8ModulationModes& jmModes = callDce.GetRemoteMenu().value().modulationModes.value();
    EXPECT_TRUE(!jmModes.v34Duplex); // not common -- must not be claimed
    EXPECT_TRUE(jmModes.v21);        // common -- correctly carried through

    const auto selected = SelectModulationMode(jmModes);
    EXPECT_TRUE(selected.has_value());
    if (selected.has_value()) {
        EXPECT_EQ(static_cast<int>(selected.value()), static_cast<int>(V8ModulationModeItem::V21));
    }
}

TEST(V8Handshake_BeforeAnythingArrives_OutcomeIsInProgressNotNegotiated) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceTest");
    V8Handshake callDce(V8Handshake::Role::CallDce, MakeDataMenu(true, true), callLogger);

    std::vector<Sample> tx;
    std::vector<Sample> silence(160, 0.0f);
    callDce.ProcessFrame(silence, tx, 160);

    EXPECT_EQ(static_cast<int>(callDce.GetOutcome()), static_cast<int>(V8HandshakeOutcome::InProgress));
    EXPECT_EQ(tx.size(), static_cast<size_t>(160));
}

namespace {

// Feeds `seconds` of audio produced by `nextSample` to a call DCE in 20ms
// frames; returns the time (seconds) of the first frame where the call
// DCE transmits anything non-silent (i.e. it detected ANSam, waited out
// Te, and started CM), or -1 if it never did.
template <typename SampleSource>
double FirstTransmitTimeSeconds(V8Handshake& callDce, SampleSource nextSample, double seconds) {
    constexpr int kFrame = 160;
    std::vector<Sample> rx(kFrame);
    std::vector<Sample> tx;
    const int frames = static_cast<int>(seconds * 8000.0 / kFrame);
    for (int f = 0; f < frames; ++f) {
        for (auto& s : rx) {
            s = nextSample();
        }
        callDce.ProcessFrame(rx, tx, kFrame);
        for (Sample s : tx) {
            if (s != 0.0f) {
                return f * kFrame / 8000.0;
            }
        }
    }
    return -1.0;
}

// ~-12 dBm0 in G.711: a typical real modem's transmit level (peak ~0.17).
constexpr float kRealModemAmplitude = 0.17f;

} // namespace

// Regression for the 2026-11-24 live call against a real V.92 softmodem:
// 5s of genuine ANSam at a real modem's level was never detected because
// the old fixed 0.2 threshold assumed this codebase's own hot generator.
TEST(V8Handshake_CallDce_DetectsAnsamAtRealModemLevel) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceRealLevel");
    V8Handshake callDce(V8Handshake::Role::CallDce, MakeDataMenu(false, true), callLogger);
    AnsamGenerator ansam(kRealModemAmplitude);

    const double t = FirstTransmitTimeSeconds(callDce, [&] { return ansam.NextSample(); }, 3.0);
    EXPECT_TRUE(t > 0.0);
    EXPECT_TRUE(t < 1.0); // detection (~0.1s) + Te (0.5s)
}

TEST(V8Handshake_CallDce_DetectsQuietAnsam) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceQuiet");
    V8Handshake callDce(V8Handshake::Role::CallDce, MakeDataMenu(false, true), callLogger);
    AnsamGenerator ansam(0.04f); // ~-25 dBm0

    EXPECT_TRUE(FirstTransmitTimeSeconds(callDce, [&] { return ansam.NextSample(); }, 3.0) > 0.0);
}

// The same real modem sent V.8bis CRe (1375Hz + 2002Hz dual tone) three
// times before ANSam. 2002Hz leaks into a 20ms 2100Hz Goertzel bin, so a
// level-only threshold would start CM in the middle of V.8bis.
TEST(V8Handshake_CallDce_IgnoresV8bisDualTone) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceCre");
    V8Handshake callDce(V8Handshake::Role::CallDce, MakeDataMenu(false, true), callLogger);
    long n = 0;
    auto cre = [&] {
        const double t = static_cast<double>(n++) / 8000.0;
        return static_cast<Sample>(kRealModemAmplitude * (std::sin(2.0 * 3.14159265358979 * 1375.0 * t) +
                                                          std::sin(2.0 * 3.14159265358979 * 2002.0 * t)));
    };
    EXPECT_TRUE(FirstTransmitTimeSeconds(callDce, cre, 3.0) < 0.0);
}

TEST(V8Handshake_CallDce_IgnoresShortAnsamBurst) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceBurst");
    V8Handshake callDce(V8Handshake::Role::CallDce, MakeDataMenu(false, true), callLogger);
    AnsamGenerator ansam(kRealModemAmplitude);
    long n = 0;
    // 60ms of 2100Hz, then silence: shorter than the 100ms requirement.
    auto burst = [&] { return (n++ < 480) ? ansam.NextSample() : 0.0f; };
    EXPECT_TRUE(FirstTransmitTimeSeconds(callDce, burst, 2.0) < 0.0);
}

// Regression for the second 2026-11-24 live call: after the ANSam fix, the
// real V.92 modem sat in "V.8: JM" because its JM (~0.03 amplitude, same
// as its measured ANSam) never crossed the old fixed 0.05 sync gate. Runs
// the full handshake with every signal attenuated by 0.05x in both
// directions (our 0.6 TX -> 0.03 received, matching the live measurement).
TEST(V8Handshake_FullLoopback_NegotiatesAtRealModemLevels) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceAttenuated");
    TaggedLogger answerLogger(logger, "AnswerDceAttenuated");
    V8Handshake callDce(V8Handshake::Role::CallDce, MakeDataMenu(false, true), callLogger);
    V8Handshake answerDce(V8Handshake::Role::AnswerDce, MakeDataMenu(true, true), answerLogger);

    constexpr int kFrame = 160;
    constexpr float kLineGain = 0.05f;
    std::vector<Sample> callTx(kFrame, 0.0f);
    std::vector<Sample> answerTx(kFrame, 0.0f);
    for (int frame = 0; frame < 2000; ++frame) {
        if (callDce.GetOutcome() == V8HandshakeOutcome::Negotiated &&
            answerDce.GetOutcome() == V8HandshakeOutcome::Negotiated) {
            break;
        }
        std::vector<Sample> rxForCall = answerTx;
        std::vector<Sample> rxForAnswer = callTx;
        for (auto& s : rxForCall) s *= kLineGain;
        for (auto& s : rxForAnswer) s *= kLineGain;
        callDce.ProcessFrame(rxForCall, callTx, kFrame);
        answerDce.ProcessFrame(rxForAnswer, answerTx, kFrame);
    }
    EXPECT_TRUE(callDce.GetOutcome() == V8HandshakeOutcome::Negotiated);
    EXPECT_TRUE(answerDce.GetOutcome() == V8HandshakeOutcome::Negotiated);
}
