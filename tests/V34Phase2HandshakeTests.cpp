#include "../src/Lib/Dsp/V34Phase2Handshake.h"
#include "../src/Lib/Logging/Logger.h"
#include "../src/Lib/Logging/TaggedLogger.h"
#include "TestFramework.h"

#include <cmath>
#include <deque>

using namespace voipmodem::dsp;
using voipmodem::Logger;
using voipmodem::TaggedLogger;

namespace {
constexpr Sample kNominalAmplitude = 0.8f;
constexpr int kFrameSampleCount = 160; // 20ms @ 8kHz, matches CallAudioBridge's real cadence

// Same idea as V8HandshakeTests' RunLoopbackToCompletion, but with an
// explicit one-way line delay (in samples) inserted in both
// directions, via a small FIFO per direction -- needed here because,
// unlike V8Handshake, this class's whole job this increment is to
// measure that very delay (RTDEc/RTDEa), so a zero-delay loopback
// would only prove the mechanics run, not that the arithmetic is
// right.
class DelayLine {
public:
    explicit DelayLine(int delaySamples) : delaySamples_(delaySamples) {
        buffer_.assign(static_cast<size_t>(delaySamples), 0.0f);
    }

    std::vector<Sample> Push(const std::vector<Sample>& in) {
        buffer_.insert(buffer_.end(), in.begin(), in.end());
        std::vector<Sample> out(buffer_.begin(), buffer_.begin() + static_cast<long>(in.size()));
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(in.size()));
        return out;
    }

private:
    int delaySamples_;
    std::deque<Sample> buffer_;
};

// Runs both sides until both report RoundTripDelayMeasured or
// `maxFrames` elapses. `oneWayDelaySamples` models a symmetric line
// delay in each direction (so the true round trip is 2x that).
bool RunLoopbackToCompletion(V34Phase2Handshake& callDce, V34Phase2Handshake& answerDce, int oneWayDelaySamples,
                              int maxFrames) {
    DelayLine callToAnswer(oneWayDelaySamples);
    DelayLine answerToCall(oneWayDelaySamples);
    std::vector<Sample> callTx;
    std::vector<Sample> answerTx;
    const std::vector<Sample> silence(static_cast<size_t>(kFrameSampleCount), 0.0f);

    callDce.ProcessFrame(silence, callTx, kFrameSampleCount);
    answerDce.ProcessFrame(silence, answerTx, kFrameSampleCount);

    for (int frame = 0; frame < maxFrames; ++frame) {
        if (callDce.GetOutcome() == V34Phase2Outcome::RoundTripDelayMeasured &&
            answerDce.GetOutcome() == V34Phase2Outcome::RoundTripDelayMeasured) {
            return true;
        }

        const std::vector<Sample> rxForCallDce = answerToCall.Push(answerTx);
        const std::vector<Sample> rxForAnswerDce = callToAnswer.Push(callTx);

        callDce.ProcessFrame(rxForCallDce, callTx, kFrameSampleCount);
        answerDce.ProcessFrame(rxForAnswerDce, answerTx, kFrameSampleCount);
    }

    return callDce.GetOutcome() == V34Phase2Outcome::RoundTripDelayMeasured &&
           answerDce.GetOutcome() == V34Phase2Outcome::RoundTripDelayMeasured;
}

// Same loopback mechanics, but keeps running past RTD measurement
// until the call DCE reports L1 received (11.2.1.1.4/.5's next step)
// or `maxFrames` elapses.
bool RunLoopbackUntilL1Received(V34Phase2Handshake& callDce, V34Phase2Handshake& answerDce, int oneWayDelaySamples,
                                  int maxFrames) {
    DelayLine callToAnswer(oneWayDelaySamples);
    DelayLine answerToCall(oneWayDelaySamples);
    std::vector<Sample> callTx;
    std::vector<Sample> answerTx;
    const std::vector<Sample> silence(static_cast<size_t>(kFrameSampleCount), 0.0f);

    callDce.ProcessFrame(silence, callTx, kFrameSampleCount);
    answerDce.ProcessFrame(silence, answerTx, kFrameSampleCount);

    for (int frame = 0; frame < maxFrames; ++frame) {
        if (callDce.GetHasReceivedL1()) {
            return true;
        }

        const std::vector<Sample> rxForCallDce = answerToCall.Push(answerTx);
        const std::vector<Sample> rxForAnswerDce = callToAnswer.Push(callTx);

        callDce.ProcessFrame(rxForCallDce, callTx, kFrameSampleCount);
        answerDce.ProcessFrame(rxForAnswerDce, answerTx, kFrameSampleCount);
    }

    return callDce.GetHasReceivedL1();
}

// Same loopback mechanics, but keeps running until BOTH sides report
// Phase2Complete (the full 11.2.1.1/11.2.1.2 procedure, INFO1a/INFO1c
// included) or `maxFrames` elapses.
bool RunLoopbackUntilPhase2Complete(V34Phase2Handshake& callDce, V34Phase2Handshake& answerDce,
                                      int oneWayDelaySamples, int maxFrames) {
    DelayLine callToAnswer(oneWayDelaySamples);
    DelayLine answerToCall(oneWayDelaySamples);
    std::vector<Sample> callTx;
    std::vector<Sample> answerTx;
    const std::vector<Sample> silence(static_cast<size_t>(kFrameSampleCount), 0.0f);

    callDce.ProcessFrame(silence, callTx, kFrameSampleCount);
    answerDce.ProcessFrame(silence, answerTx, kFrameSampleCount);

    for (int frame = 0; frame < maxFrames; ++frame) {
        if (callDce.GetOutcome() == V34Phase2Outcome::Phase2Complete &&
            answerDce.GetOutcome() == V34Phase2Outcome::Phase2Complete) {
            return true;
        }

        const std::vector<Sample> rxForCallDce = answerToCall.Push(answerTx);
        const std::vector<Sample> rxForAnswerDce = callToAnswer.Push(callTx);

        callDce.ProcessFrame(rxForCallDce, callTx, kFrameSampleCount);
        answerDce.ProcessFrame(rxForAnswerDce, answerTx, kFrameSampleCount);
    }

    return callDce.GetOutcome() == V34Phase2Outcome::Phase2Complete &&
           answerDce.GetOutcome() == V34Phase2Outcome::Phase2Complete;
}
} // namespace

TEST(V34Phase2Handshake_FullLoopback_BothSidesMeasureRoundTripDelay) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceTest");
    TaggedLogger answerLogger(logger, "AnswerDceTest");

    V34Phase2Handshake callDce(V34Phase2Handshake::Role::CallDce, kNominalAmplitude, callLogger);
    V34Phase2Handshake answerDce(V34Phase2Handshake::Role::AnswerDce, kNominalAmplitude, answerLogger);

    const bool completed = RunLoopbackToCompletion(callDce, answerDce, /*oneWayDelaySamples=*/0, /*maxFrames=*/400);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(callDce.GetOutcome() == V34Phase2Outcome::RoundTripDelayMeasured);
    EXPECT_TRUE(answerDce.GetOutcome() == V34Phase2Outcome::RoundTripDelayMeasured);
}

TEST(V34Phase2Handshake_FullLoopback_BothSidesLearnTheOthersInfo0) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceTest2");
    TaggedLogger answerLogger(logger, "AnswerDceTest2");

    V34Phase2Handshake callDce(V34Phase2Handshake::Role::CallDce, kNominalAmplitude, callLogger);
    V34Phase2Handshake answerDce(V34Phase2Handshake::Role::AnswerDce, kNominalAmplitude, answerLogger);

    RunLoopbackToCompletion(callDce, answerDce, /*oneWayDelaySamples=*/0, /*maxFrames=*/400);

    EXPECT_TRUE(callDce.GetRemoteInfo0().has_value());
    EXPECT_TRUE(answerDce.GetRemoteInfo0().has_value());
}

// A real line delay, large enough to dwarf the detector's own ~5ms
// window-based timing resolution (TonePhaseReversalDetector.h), so the
// computed RTD should land close to the known, injected round trip
// (2x the one-way delay) rather than just close to zero.
TEST(V34Phase2Handshake_WithSimulatedLineDelay_ComputesASaneNonZeroRoundTripDelay) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceDelayTest");
    TaggedLogger answerLogger(logger, "AnswerDceDelayTest");

    V34Phase2Handshake callDce(V34Phase2Handshake::Role::CallDce, kNominalAmplitude, callLogger);
    V34Phase2Handshake answerDce(V34Phase2Handshake::Role::AnswerDce, kNominalAmplitude, answerLogger);

    const int oneWayDelaySamples = 400; // 50ms one way @ 8kHz -> 100ms round trip
    const bool completed = RunLoopbackToCompletion(callDce, answerDce, oneWayDelaySamples, /*maxFrames=*/600);

    EXPECT_TRUE(completed);
    if (completed) {
        // The loopback harness itself adds one frame's worth of extra
        // one-way latency on top of the DelayLine (each side's TX from
        // iteration N only reaches the other side's RX in iteration
        // N+1) -- a real property of this synchronous simulation, the
        // same way any real framed-audio pipeline has per-frame
        // latency, not a bug to work around.
        const double expectedRoundTripSeconds =
            2.0 * (oneWayDelaySamples + kFrameSampleCount) / static_cast<double>(kSampleRateHz);
        const double tolerance = 0.020; // 20ms -- generous vs. the detector's ~5ms window and frame-boundary slop
        EXPECT_TRUE(std::abs(callDce.GetRoundTripDelaySeconds() - expectedRoundTripSeconds) < tolerance);
        EXPECT_TRUE(std::abs(answerDce.GetRoundTripDelaySeconds() - expectedRoundTripSeconds) < tolerance);
    }
}

TEST(V34Phase2Handshake_AfterRtdMeasured_CallDceReceivesL1) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceL1Test");
    TaggedLogger answerLogger(logger, "AnswerDceL1Test");

    V34Phase2Handshake callDce(V34Phase2Handshake::Role::CallDce, kNominalAmplitude, callLogger);
    V34Phase2Handshake answerDce(V34Phase2Handshake::Role::AnswerDce, kNominalAmplitude, answerLogger);

    EXPECT_TRUE(!callDce.GetHasReceivedL1());

    const bool received = RunLoopbackUntilL1Received(callDce, answerDce, /*oneWayDelaySamples=*/0, /*maxFrames=*/800);

    EXPECT_TRUE(received);
    EXPECT_TRUE(callDce.GetHasReceivedL1());
}

// The big one: runs the ENTIRE error-free Phase 2 procedure on both
// sides, start to finish -- both Tone A/B reversal exchanges and their
// RTD calculations, both L1/L2 probing exchanges, and the INFO1c/INFO1a
// exchange that hands off into Phase 3. This is what makes
// "implements through INFO1a/INFO1c" an exercised claim, not a hoped
// one.
TEST(V34Phase2Handshake_FullLoopback_ReachesPhase2Complete) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceFullTest");
    TaggedLogger answerLogger(logger, "AnswerDceFullTest");

    V34Phase2Handshake callDce(V34Phase2Handshake::Role::CallDce, kNominalAmplitude, callLogger);
    V34Phase2Handshake answerDce(V34Phase2Handshake::Role::AnswerDce, kNominalAmplitude, answerLogger);

    const bool completed =
        RunLoopbackUntilPhase2Complete(callDce, answerDce, /*oneWayDelaySamples=*/0, /*maxFrames=*/3000);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(callDce.GetOutcome() == V34Phase2Outcome::Phase2Complete);
    EXPECT_TRUE(answerDce.GetOutcome() == V34Phase2Outcome::Phase2Complete);
    EXPECT_TRUE(callDce.GetHasReceivedL1());

    // Answer DCE received the call DCE's INFO1c; call DCE received the
    // answer DCE's INFO1a.
    EXPECT_TRUE(answerDce.GetRemoteInfo1c().has_value());
    EXPECT_TRUE(callDce.GetRemoteInfo1a().has_value());
    if (answerDce.GetRemoteInfo1c().has_value()) {
        // Honestly 0 -- see V34Info1.h's class comment. Confirms the
        // default (not some other, accidentally-nonzero) value made it
        // all the way across.
        EXPECT_EQ(static_cast<int>(answerDce.GetRemoteInfo1c()->rate2400.projectedDataRateMultiplier), 0);
    }
}

TEST(V34Phase2Handshake_WithSimulatedLineDelay_ReachesPhase2Complete) {
    Logger logger;
    TaggedLogger callLogger(logger, "CallDceFullDelayTest");
    TaggedLogger answerLogger(logger, "AnswerDceFullDelayTest");

    V34Phase2Handshake callDce(V34Phase2Handshake::Role::CallDce, kNominalAmplitude, callLogger);
    V34Phase2Handshake answerDce(V34Phase2Handshake::Role::AnswerDce, kNominalAmplitude, answerLogger);

    const bool completed =
        RunLoopbackUntilPhase2Complete(callDce, answerDce, /*oneWayDelaySamples=*/400, /*maxFrames=*/4000);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(callDce.GetOutcome() == V34Phase2Outcome::Phase2Complete);
    EXPECT_TRUE(answerDce.GetOutcome() == V34Phase2Outcome::Phase2Complete);
}
