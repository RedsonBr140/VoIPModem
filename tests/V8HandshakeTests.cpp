#include "../src/Lib/Dsp/V8Handshake.h"
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
