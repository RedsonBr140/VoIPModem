#pragma once

#include <deque>
#include <optional>
#include <vector>

#include "AnsamTone.h"
#include "DspTypes.h"
#include "FskDemodulator.h"
#include "FskModulator.h"
#include "V8CallMenu.h"
#include "../Logging/TaggedLogger.h"

namespace voipmodem::dsp {

enum class V8HandshakeOutcome { InProgress, Negotiated, Failed };

// Drives one side (call or answer DCE) of the ITU-T V.8 handshake
// (clause 8) across real call audio, one audio frame at a time --
// mirroring CallAudioBridge's existing onFrameReceived/onFrameRequested
// cadence (nominally 160 samples / 20ms at 8kHz, but any frame size
// works). Channel assignment matches CallAudioBridge/FskModulator's
// existing convention: calling party (Role::CallDce) transmits CM on
// V.21 channel 1 and receives JM on channel 2; answering party
// (Role::AnswerDce) transmits JM on channel 2 and receives CM on
// channel 1.
//
// CI is deliberately not implemented: 7.1 states CI's use is optional,
// and a conforming call DCE "should not malfunction" if it's absent,
// so the call DCE here starts straight from ANSam detection.
//
// Honestly simplified vs. the full clause 8 text -- see
// docs/ROADMAP.md Phase 4 for the fuller account:
// - Te (the silent period before CM) is fixed at the spec's own
//   minimum, 0.5s, not configurable or extended for echo-canceller
//   disabling (V.25's >=1s option).
// - The answer DCE's initial silence is fixed at the spec's own
//   minimum, 0.2s.
// - No retry/timeout handling at all: if ANSam, CM, JM, or CJ never
//   arrives, this sits in Outcome::InProgress forever rather than
//   timing out. `Outcome::Failed` exists in the enum but nothing in
//   this class ever produces it yet.
// - CJ detection (the answer DCE waiting for the call DCE's CJ) scans
//   for the 3-zero-octet pattern bit-by-bit from a resume cursor that
//   only ever advances (never re-checks a position twice), so it does
//   tolerate the call DCE's CM-to-CJ transition landing mid-frame
//   rather than on an octet boundary. It still has no way to recover
//   from genuinely corrupted audio, and there's no timeout -- if CJ
//   never arrives at all, this sits waiting forever.
// - The 75ms silent gaps the real protocol inserts around CJ/sigA/sigC
//   (Figure 1) aren't modeled, and this class says nothing about sigA/
//   sigC at all -- that's the actual V.32bis/V.34/etc. modem training,
//   out of scope for V.8 itself, same as the Recommendation scopes it.
// - JM's content is computed as the real intersection of CM's
//   modulation modes and this side's own local menu (7.4), which is
//   more than a placeholder, but nothing here picks a *different* call
//   function when the requested one isn't available (8.2.3) -- this
//   implementation only ever offers V8CallFunction::Data.
// - No real symbol-timing/clock recovery exists anywhere in this
//   codebase. A receiving FskDemodulator that starts ticking its own
//   bit-clock well before the other side's signal actually arrives
//   (e.g. answer DCE waiting through ANSam before CM starts) ends up
//   phase-misaligned with the transmitter's real bit edges -- found
//   live in this class's own loopback test, where it caused CM/JM to
//   never be recognized at all despite being transmitted correctly.
//   The fix here (`cmDemodulator_`/`jmDemodulator_` as
//   `std::optional`, lazily constructed the moment real energy at that
//   channel's own mark/space frequencies is first seen) is a pragmatic
//   edge-sync substitute, not real clock recovery. The frequency check
//   matters, not just an amplitude check: the call DCE's JM channel
//   sits silent while ANSam (2100 Hz) is still playing on the shared
//   audio during the answer DCE's CM-watching phase, which is loud but
//   not at JM's own frequencies -- an amplitude-only check synced to
//   that instead of real JM and reintroduced this exact bug. This
//   still assumes each frame is homogeneous in content (true in this
//   implementation, since state transitions fall on frame boundaries)
//   and would not survive a noisy real line or genuine co-channel
//   interference.
// - Found live testing two real VoIPModem instances calling each other
//   over real Asterisk/RTP audio (2026-10-17, same day as the above):
//   even the frequency-selective sync above wasn't enough. RTP/jitter-
//   buffer latency (observed: ~400ms) means the call DCE can still be
//   *receiving* the answer DCE's already-stopped ANSam well after the
//   answer DCE's own state machine has moved on to transmitting JM --
//   the frequency check alone doesn't catch this because the audio IS
//   genuinely absent at JM's frequencies right up until the moment it
//   crosses over, and sync fired on that transitional crossover rather
//   than JM's true onset, permanently misaligning the receiver. Fixed
//   with an additional, protocol-specific gate: the call DCE's JM sync
//   also requires ANSam's own 2100Hz energy to have actually dropped
//   first (`FeedSyncedDemodulator`'s `competingHz`/`competingThreshold`
//   parameters), not just energy appearing at JM's frequencies. This is
//   still a pragmatic, narrowly-targeted patch, not real symbol-timing
//   recovery -- it only works because this specific transition's
//   "competing tone" is a known, fixed frequency (ANSam's 2100Hz); it
//   would not generalize to an arbitrary noisy line.
class V8Handshake {
public:
    enum class Role { CallDce, AnswerDce };

    V8Handshake(Role role, V8CallMenu localMenu, TaggedLogger& logger);

    // Feeds one frame's worth of received samples; replaces the
    // contents of `txSamplesOut` with exactly `frameSampleCount`
    // samples to transmit for that same frame.
    void ProcessFrame(const std::vector<Sample>& rxSamples, std::vector<Sample>& txSamplesOut, int frameSampleCount);

    V8HandshakeOutcome GetOutcome() const { return outcome_; }

    // Valid once GetOutcome() == Negotiated: whatever the *other* side
    // told this side it has (the call DCE's received JM; the answer
    // DCE's received, matched CM).
    const std::optional<V8CallMenu>& GetRemoteMenu() const { return remoteMenu_; }

private:
    enum class State {
        WaitingForAnsam,
        SilentTe,
        TransmittingCmWaitingForJm,
        SendingCj,
        InitialSilence,
        TransmittingAnsamWaitingForCm,
        TransmittingJmWaitingForCj,
        Negotiated,
    };

    void ProcessCallDceFrame(const std::vector<Sample>& rxSamples, std::vector<Sample>& txSamplesOut,
                              int frameSampleCount);
    void ProcessAnswerDceFrame(const std::vector<Sample>& rxSamples, std::vector<Sample>& txSamplesOut,
                                int frameSampleCount);

    // Shared by both roles: lazily syncs+constructs `demod` the moment
    // real signal is first seen (see the class comment's symbol-timing
    // note), appends the bits demodulated this frame, searches for the
    // next CM/JM preamble from `searchResumeIndex` onward, and on a
    // full menu read, compacts `bits` and rebases `searchResumeIndex`
    // to 0 -- bounded-growth discipline, see V8Signal.h's
    // FindBitPattern comment for why that matters.
    // `competingHz`/`competingThreshold` (optional, 0.0 disables): see
    // FeedSyncedDemodulator's comment in the .cpp -- withholds sync
    // until a specific competing tone (e.g. the call DCE's JM wait
    // guarding against lingering ANSam) has also quieted down.
    std::optional<V8CallMenu> ReceiveMenu(std::optional<FskDemodulator>& demod, double markHz, double spaceHz,
                                           const std::vector<Sample>& rxSamples, std::vector<bool>& bits,
                                           size_t& searchResumeIndex, double competingHz = 0.0,
                                           float competingThreshold = 0.0f);

    static void FillSilence(std::vector<Sample>& out, int frameSampleCount);
    static void FillFromRepeatingSequence(std::vector<Sample>& out, int frameSampleCount,
                                           std::deque<Sample>& pendingTx, FskModulator& modulator,
                                           const V8CallMenu& menu);

    Role role_;
    V8CallMenu localMenu_;
    TaggedLogger& logger_;
    State state_;
    V8HandshakeOutcome outcome_ = V8HandshakeOutcome::InProgress;
    std::optional<V8CallMenu> remoteMenu_;

    // Call DCE state
    AnsamDetector ansamDetector_;
    int silenceSamplesRemaining_ = 0;
    FskModulator cmModulator_;
    std::deque<Sample> pendingCmTx_;
    std::optional<FskDemodulator> jmDemodulator_; // lazily synced -- see class comment
    std::vector<bool> jmBits_;
    size_t jmSearchResumeIndex_ = 0;
    std::optional<V8CallMenu> lastJmMenu_;
    int identicalJmCount_ = 0;
    std::deque<Sample> pendingCjTx_;

    // Answer DCE state
    AnsamGenerator ansamGenerator_;
    FskModulator jmModulator_;
    std::deque<Sample> pendingJmTx_;
    V8CallMenu jmMenu_; // computed once CM is matched: intersected modes
    std::optional<FskDemodulator> cmDemodulator_; // lazily synced -- see class comment
    std::vector<bool> cmBits_;
    size_t cmSearchResumeIndex_ = 0;
    std::optional<V8CallMenu> lastCmMenu_;
    int identicalCmCount_ = 0;
};

} // namespace voipmodem::dsp
