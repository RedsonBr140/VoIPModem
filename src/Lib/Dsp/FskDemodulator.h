#pragma once

#include <cstdint>
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
// Still, honestly, missing: real symbol-timing recovery. Bit boundaries
// are only correct because this object and the far FskModulator both
// start counting from their own call's start, not because of anything
// derived from the signal itself.
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
    double exactSamplesPerBit_;

    GoertzelDetector markDetector_;
    GoertzelDetector spaceDetector_;
    double bitLengthRemainder_ = 0.0; // fractional carry, same technique as FskModulator's
    int currentBitTargetLength_ = -1; // -1 == not yet decided for the bit in progress
    int samplesIntoCurrentBit_ = 0;
};

} // namespace voipmodem::dsp
