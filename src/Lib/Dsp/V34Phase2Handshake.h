#pragma once

#include <deque>
#include <optional>
#include <vector>

#include "DbpskDemodulator.h"
#include "DbpskModulator.h"
#include "DspTypes.h"
#include "GoertzelDetector.h"
#include "LineProbingSignalGenerator.h"
#include "TonePhaseReversalDetector.h"
#include "V34Info0.h"
#include "V34InfoSequence.h"
#include "V34ProbingTones.h"
#include "../Logging/TaggedLogger.h"

namespace voipmodem::dsp {

enum class V34Phase2Outcome { InProgress, RoundTripDelayMeasured, Failed };

// Drives one side (call or answer DCE) of ITU-T V.34 clause 11.2's
// Phase 2 (network interaction/probing) procedure across real call
// audio, one frame at a time -- same ProcessFrame cadence and overall
// architecture as V8Handshake (lazily-constructed RX detectors at each
// decision point, a pending-TX sample queue drained before falling
// back to live generation), confirmed against the real Recommendation
// text re-read in full for this increment.
//
// HONESTLY scoped to 11.2.1.1.1-11.2.1.1.4 (call modem), 11.2.1.2.1-
// 11.2.1.2.5 (answer modem) -- the INFO0 exchange and the first full
// Tone A/Tone B phase-reversal exchange that produces each side's own
// round-trip-delay estimate (RTDEc/RTDEa) -- PLUS the first, simplest
// slice of L1: the answer DCE transmits it for its fixed 160ms
// (10.1.2.4) right after .2.5's reversal hold, and the call DCE
// detects its arrival and tracks that same 160ms window
// (GetHasReceivedL1()). The answer side implements through .2.5 (not
// just its own .2.4 RTD calculation) because the call side's second
// Tone A reversal, which .1.4 needs, IS the answer side's own .2.5
// action -- both sides' RTD calculations are two sides of the same
// two 40ms-delayed reversal "echoes".
//
// L2 is deliberately NOT implemented yet, and not because it's hard
// in the same way the detector/timing pieces were: 11.2.1.1.5/
// 11.2.1.2.6's own wording ties L2's transmission duration to "the
// local echo of L2" being received back -- a real PSTN 2-wire/4-wire
// hybrid-circuit artifact that has no obvious equivalent on a digital
// VoIP bridge through Asterisk. Implementing a stand-in for that
// without first deciding what it should even mean here would be
// guessing at a requirement, not extracting one -- see docs/ROADMAP.md
// for the open question. L1 itself has no such ambiguity (clause
// 10.1.2.4 gives it a flat, fixed 160ms duration), which is why it's
// implemented here and L2 is not.
//
// Also explicitly NOT implemented: the SECOND Tone A/B reversal
// exchange that follows L1/L2 (11.2.1.1.6/11.2.1.2.6 onward),
// INFO1a/INFO1c, the handoff into Phase 3, and none of clause 11.2.2's
// retry/recovery procedure (same honestly-deferred gap V8Handshake
// itself still has). See docs/ROADMAP.md.
//
// Known simplification: a real analog modem's Tone A/B carrier is
// physically the same continuous-phase carrier used for INFO0a/INFO0c
// immediately before it; this implementation instead constructs a
// fresh ToneAGenerator/ToneBGenerator (phase 0) the moment INFO0 TX
// finishes. Harmless for the arithmetic here, since
// TonePhaseReversalDetector only ever judges *relative* phase change
// against its own baseline, never an absolute reference -- but it is
// not literally the same signal a real modem's transmitter would put
// on the line.
//
// Known timing imprecision: the RX switch from decoding INFO0 to
// watching for a tone reversal happens on a whole-frame boundary, not
// at the exact sample INFO0 finished arriving -- unlike every
// *reversal's* own timing, which is sample-accurate throughout (both
// TonePhaseReversalDetector's detection and this class's own
// scheduled-reversal TX generation split each frame at the exact
// sample an event is due). The cost is a slightly later start for the
// reversal detector's own baseline-capture window, not an error in any
// RTD value actually computed.
class V34Phase2Handshake {
public:
    enum class Role { CallDce, AnswerDce };

    V34Phase2Handshake(Role role, Sample nominalAmplitude, TaggedLogger& logger, int sampleRateHz = kSampleRateHz);

    // Feeds one frame's worth of received samples; replaces the
    // contents of `txSamplesOut` with exactly `frameSampleCount`
    // samples to transmit for that same frame.
    void ProcessFrame(const std::vector<Sample>& rxSamples, std::vector<Sample>& txSamplesOut, int frameSampleCount);

    V34Phase2Outcome GetOutcome() const { return outcome_; }

    // Valid once GetOutcome() == RoundTripDelayMeasured.
    double GetRoundTripDelaySeconds() const { return roundTripDelaySeconds_; }

    // Valid once this side has successfully decoded the other side's
    // INFO0 sequence (true well before RTD measurement completes).
    const std::optional<V34Info0>& GetRemoteInfo0() const { return remoteInfo0_; }

    // Call DCE only: true once L1's presence has been detected AND its
    // full 160ms (10.1.2.4) has elapsed since. Always false on the
    // answer DCE side (this class doesn't yet expose whether the
    // answer DCE has *finished transmitting* L1, only that the call
    // DCE finished receiving it).
    bool GetHasReceivedL1() const { return l1Received_; }

private:
    static constexpr double kSilentPeriodSeconds = 0.075;         // 75ms, the spec's own minimum/nominal
    static constexpr double kReversalHoldSeconds = 0.010;         // "transmitted for Xms after the reversal"
    static constexpr double kAnswerMinToneADurationSeconds = 0.050;
    static constexpr double kL1DurationSeconds = 0.160;            // 10.1.2.4's fixed L1 duration
    static constexpr float kTonePresenceThreshold = 0.25f;        // relative to nominalAmplitude_
    // One of L1's own Table 17 tones (150Hz), used purely as a
    // presence beacon on the RX side -- not a channel-response
    // measurement of any kind, just "has L1 started arriving yet".
    static constexpr double kL1PresenceBeaconHz = 150.0;

    void ProcessCallDceFrame(const std::vector<Sample>& rxSamples, std::vector<Sample>& txSamplesOut,
                              int frameSampleCount);
    void ProcessAnswerDceFrame(const std::vector<Sample>& rxSamples, std::vector<Sample>& txSamplesOut,
                                int frameSampleCount);

    // Generates `sampleCount` samples of the call DCE's Tone B output,
    // splitting the chunk at the exact sample its scheduled reversal
    // (11.2.1.1.3) is due if that falls within this chunk, applying it,
    // holding for kReversalHoldSeconds, then falling silent.
    void GenerateCallDceToneB(std::vector<Sample>& out, int sampleCount);

    // Generates `sampleCount` samples of the answer DCE's Tone A
    // output. Unlike the call DCE's single reversal, this handles TWO
    // sequential reversal events on the same continuous generator: the
    // first (11.2.1.2.3) has no hold/silence afterward -- Tone A just
    // continues, reversed, until the second -- while the second
    // (11.2.1.2.5) does hold then fall silent, same shape as the call
    // DCE's.
    // Called once secondReversalHoldDone_ first becomes true: emits
    // L1 for its fixed 160ms, then falls silent (L2 deliberately not
    // implemented -- see class comment). Shares GenerateAnswerDceToneA's
    // absolute-sample event-splitting style.
    void GenerateAnswerDceToneA(std::vector<Sample>& out, int sampleCount);

    // Drains pendingInfo0cTx_/pendingInfo0aTx_ first, then falls back
    // to the live Tone B/Tone A generator for however many samples are
    // left in this chunk once the queue runs dry.
    void GenerateCallDceTxChunk(std::vector<Sample>& out, int sampleCount);
    void GenerateAnswerDceTxChunk(std::vector<Sample>& out, int sampleCount);

    static void FillSilence(std::vector<Sample>& out, int frameSampleCount);

    Role role_;
    Sample nominalAmplitude_;
    int sampleRateHz_;
    TaggedLogger& logger_;
    V34Phase2Outcome outcome_ = V34Phase2Outcome::InProgress;
    long long elapsedSamples_ = 0;
    long long silenceSamplesRemaining_;
    std::optional<V34Info0> remoteInfo0_;
    double roundTripDelaySeconds_ = 0.0;

    // Own-reversal scheduling, shared shape for both roles (only one
    // role's generator fields below are ever actually used).
    bool ownReversalScheduled_ = false;
    long long ownReversalScheduledAbs_ = 0;
    bool ownReversalApplied_ = false;
    long long ownReversalAppliedAbs_ = 0;
    bool postReversalHoldDone_ = false;

    // Call DCE state
    std::deque<Sample> pendingInfo0cTx_;
    bool info0cTxQueued_ = false;
    StreamingInfo0Receiver info0aReceiver_; // constructed in the .cpp init list (needs sampleRateHz)
    bool info0aReceived_ = false;
    std::optional<ToneBGenerator> toneBGenerator_;
    std::optional<TonePhaseReversalDetector> toneADetector_;
    long long toneADetectorStartAbs_ = 0; // absolute sample toneADetector_'s own clock 0 corresponds to
    int toneAEventsSeen_ = 0;
    GoertzelDetector l1PresenceDetector_; // constructed in the .cpp init list (needs sampleRateHz)
    bool l1PresenceDetected_ = false;
    long long l1DetectionStartAbs_ = 0;
    bool l1Received_ = false;

    // Answer DCE state
    std::deque<Sample> pendingInfo0aTx_;
    bool info0aTxQueued_ = false;
    StreamingInfo0Receiver info0cReceiver_; // constructed in the .cpp init list (needs sampleRateHz)
    bool info0cReceived_ = false;
    std::optional<ToneAGenerator> toneAGenerator_;
    long long toneATxStartedAbs_ = -1;
    GoertzelDetector toneBPresenceDetector_; // constructed in the .cpp init list (needs sampleRateHz)
    bool toneBPresenceDetected_ = false;
    std::optional<TonePhaseReversalDetector> toneBDetector_;
    long long toneBDetectorStartAbs_ = 0; // absolute sample toneBDetector_'s own clock 0 corresponds to
    bool firstReversalScheduled_ = false; // the .2.3 condition-triggered reversal
    long long firstReversalScheduledAbs_ = 0;
    bool firstReversalApplied_ = false;
    long long firstReversalAppliedAbs_ = 0;
    bool secondReversalScheduled_ = false; // the .2.5 "send it again, 40ms after detecting Tone B" step
    long long secondReversalScheduledAbs_ = 0;
    bool secondReversalApplied_ = false;
    long long secondReversalAppliedAbs_ = 0;
    bool secondReversalHoldDone_ = false;
    bool l1TxStarted_ = false;
    long long l1TxEndAbs_ = 0;
    std::optional<LineProbingSignalGenerator> l1Generator_;
};

} // namespace voipmodem::dsp
