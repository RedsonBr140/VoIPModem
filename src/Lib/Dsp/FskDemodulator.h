#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "DspTypes.h"
#include "GoertzelDetector.h"

namespace voipmodem::dsp {

// Binary FSK demodulator counterpart to FskModulator: decides mark vs.
// space for each bit period by comparing GoertzelDetector magnitude at
// the two target frequencies (whichever is stronger wins).
//
// Streaming by design: ProcessSamples() carries all bit-boundary and
// in-progress-bit state (the current bit's partial Goertzel accumulators,
// how many samples into it we are, and the fractional bit-length
// remainder) as members, across calls, so a caller can feed it any
// number of new samples at a time -- a single buffer, or a live call's
// steady stream of small frames -- and only ever pays for the samples
// it hasn't seen yet.
//
// This replaces an earlier version (DemodulateBits) that took no
// internal state and instead relied on the caller re-running it over an
// ever-growing buffer from scratch each time. That was verified correct
// in isolation, but once wired into CallAudioBridge it turned a live
// call's O(1)-per-frame work into O(n) per frame with n growing without
// bound -- confirmed live (2026-10-17): after roughly a minute and a
// half of real call time, PJSIP's media thread fell far enough behind
// that no FSK bytes ever decoded and the call stopped responding to
// teardown at all. See docs/ROADMAP.md Phase 4 for the full account.
//
// Symbol-timing recovery (added 2026-10-18, after live-testing the V.8
// handshake exposed exactly this gap -- see V8Handshake.h's class
// comment and docs/ROADMAP.md Phase 4): TX and RX both run at the same
// nominal 8kHz sample clock (no separate crystals -- this is digital
// audio over RTP, not an analog line -- see DspTypes.h), so there is no
// frequency drift to track between them, only a one-time phase/start-
// of-bit offset: whatever sample a caller happens to start feeding
// ProcessSamples from (e.g. V8Handshake's lazy "sync the moment real
// energy appears" heuristic) is essentially never exactly the
// transmitter's true bit edge. Left uncorrected, every bit window after
// that point is evaluated at a fixed-but-wrong phase relative to the
// true signal, which compounds into real decode errors whenever the
// windows straddle a mark/space transition -- confirmed live, where
// this caused the handshake to intermittently stall or decode garbage.
// This is fixed with a standard early-late gate: a few samples after
// each bit is decided, the same winning tone (whichever of mark/space
// won) is re-measured in a window shifted slightly earlier and slightly
// later than where this object actually placed it; whichever side has
// more energy indicates which direction the true edge lies, and a
// small, clamped nudge is folded into the same fractional
// `bitLengthRemainder_` accumulator FskModulator already uses for its
// own +-1 sample/bit rate compensation -- so convergence happens over
// several bits, not in one potentially-noisy step.
class FskDemodulator {
public:
    FskDemodulator(double markHz, double spaceHz, double baudRate, int sampleRateHz = kSampleRateHz);

    // Feeds new samples and returns the bits (zero or more) they
    // completed. Safe to call repeatedly with small chunks as they
    // arrive -- no growing buffer, no re-processing of samples already
    // seen in an earlier call.
    std::vector<bool> ProcessSamples(const std::vector<Sample>& samples);

    // Looks for a V.21/V.8-style async-framed byte (one space/0 start
    // bit, 8 data bits LSB first, one mark/1 stop bit) starting at
    // `bits[startIndex]`. Returns the decoded byte and leaves
    // `startIndex` pointing just past the stop bit on success; leaves
    // `startIndex` unchanged and returns std::nullopt if there aren't 10
    // bits available or framing doesn't match (wrong start/stop bit) --
    // the latter means `bits[startIndex]` wasn't actually a start bit,
    // not that the byte was corrupted in transit.
    static std::optional<std::uint8_t> TryDecodeAsyncByte(const std::vector<bool>& bits, size_t& startIndex);

private:
    void RecordHistory(Sample sample);
    void MaybeApplyPendingTimingCorrection();

    double markHz_;
    double spaceHz_;
    int sampleRateHz_;
    double exactSamplesPerBit_;

    GoertzelDetector markDetector_;
    GoertzelDetector spaceDetector_;
    double bitLengthRemainder_ = 0.0; // fractional carry, same technique as FskModulator's
    int currentBitTargetLength_ = -1; // -1 == not yet decided for the bit in progress
    int samplesIntoCurrentBit_ = 0;

    // Early-late gate state (see class comment). `sampleHistory_` is a
    // bounded ring of the most recent raw samples -- just enough to
    // cover one bit plus the early/late lookback margin on each side --
    // so a bit's window can be re-measured slightly shifted without
    // needing to re-receive anything.
    std::deque<Sample> sampleHistory_;
    long long totalSamplesSeen_ = 0;
    int timingDelta_; // early/late shift, in samples, derived from the baud rate
    bool hasPendingCorrection_ = false;
    long long pendingBitStartAbs_ = 0;
    int pendingBitLen_ = 0;
    bool pendingBitWasMark_ = false;
    // Classic PI loop filter driven by the early-late error: the
    // proportional term (folded straight into bitLengthRemainder_)
    // reacts immediately to a phase error but, on its own, can only
    // settle at a nonzero steady-state error against a *sustained* rate
    // mismatch (textbook P-only control) -- which is exactly the
    // mismatch a jitter buffer's accelerate/expand concealment
    // produces. `estimatedRateErrorSamplesPerBit_` is the integral
    // term: it slowly accumulates a persistent correction to the
    // average bit length itself, so sustained drift gets tracked out
    // rather than just permanently fought bit-by-bit.
    double estimatedRateErrorSamplesPerBit_ = 0.0;

    // Added 2026-11-23 after live-testing found a real bug: a long run of
    // identical bits (e.g. the continuous mark "idle" tone CallAudioBridge
    // sends between bytes -- see CallAudioBridge.h's "DATA MODE" section)
    // carries no genuine edge-timing information at all -- the early and
    // late windows are both just measuring the same steady tone, so any
    // nonzero reading is Goertzel-windowing noise, not a real timing
    // error. Feeding that noise into the PI loop for thousands of idle
    // bit periods let `estimatedRateErrorSamplesPerBit_` drift to a real,
    // wrong, sustained bias -- confirmed live: a message's very first
    // byte decoded wrong immediately after a multi-second idle gap, then
    // the rest of the message decoded perfectly once real transitions
    // gave the loop legitimate error signal to correct from again. Fix:
    // only trust the early-late measurement on a bit that's different
    // from the one before it (an actual transition) -- skip scheduling a
    // correction otherwise. Real data has plenty of transitions (the
    // async frame's own start/stop bits guarantee at least two per byte),
    // so this costs essentially nothing during real traffic.
    bool hasPreviousDecidedBit_ = false;
    bool previousDecidedBit_ = false;
};

} // namespace voipmodem::dsp
