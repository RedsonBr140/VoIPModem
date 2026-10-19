#pragma once

#include <vector>

#include "Dds.h"
#include "DspTypes.h"

namespace voipmodem::dsp {

// Generates ITU-T V.34's Tone A (clause 10.1.2.1) and Tone B (clause
// 10.1.2.2) -- confirmed against the real Recommendation V.34 (02/98)
// text (its public PDF, same pdftotext approach used for the rest of
// this module's V.34 work -- see docs/ROADMAP.md Phase 4), not guessed.
//
// Both are continuous single-frequency tones used only during Phase 2
// (line probing) to measure round-trip delay: each side listens for a
// 180-degree phase reversal in the *other* side's tone, replies with a
// reversal of its own after a fixed 40ms delay, and the round-trip
// timing of that exchange is the round-trip-delay estimate
// (11.2.1.1/11.2.1.2, RTDEc/RTDEa). These classes only generate the
// tone and let a caller trigger a reversal on demand -- they say
// nothing about *when* to do so during the exchange; that's entirely
// clause 11.2's sequencing/timing/retry state machine, not implemented
// yet.
//
// Deliberately scoped to generation only, the same split already kept
// for FskModulator/FskDemodulator and DbpskModulator/DbpskDemodulator:
// *detecting* a phase reversal, and critically measuring exactly when
// one occurred (which is what round-trip-delay measurement actually
// needs), is a separate, harder problem -- not yet implemented. See
// docs/ROADMAP.md for what that still requires.
class ToneWithPhaseReversal {
public:
    ToneWithPhaseReversal(double toneHz, Sample amplitude, int sampleRateHz = kSampleRateHz);

    // One sample of the tone at its current phase.
    Sample NextSample();

    // Appends `sampleCount` samples of the tone at its current phase.
    void GenerateSamples(std::vector<Sample>& out, int sampleCount);

    // Applies a 180-degree phase jump, effective starting with the very
    // next sample -- 10.1.2.1/10.1.2.2's "A to A'"/"B to B'" transition.
    void ReversePhase();

private:
    Dds dds_;
    Sample amplitude_;
};

// 10.1.2.1: Tone A (2400 Hz, transmitted by the answer modem) is sent
// 1 dB below the nominal transmit power, while a separate, continuous
// 1800 Hz guard tone -- no phase reversals of its own -- runs at the
// nominal power for as long as Tone A is being sent. `nominalAmplitude`
// is the amplitude representing 0 dB for this call; Tone A's own
// amplitude is derived from it (-1 dB = x10^(-1/20)) rather than taken
// as a separate parameter, so the two can't accidentally end up out of
// the ratio the Recommendation specifies.
class ToneAGenerator {
public:
    explicit ToneAGenerator(Sample nominalAmplitude, int sampleRateHz = kSampleRateHz);

    void GenerateSamples(std::vector<Sample>& out, int sampleCount);

    // Reverses Tone A's own 2400 Hz phase; the 1800 Hz guard tone never
    // reverses, per 10.1.2.1.
    void ReversePhase();

private:
    ToneWithPhaseReversal tone_; // 2400 Hz, 1 dB below nominal
    Dds guardToneDds_;          // 1800 Hz, nominal power, never reversed
    Sample guardAmplitude_;
};

// 10.1.2.2: Tone B (1200 Hz), transmitted by the call modem. That
// clause's text describes only the tone and its phase reversals --
// unlike Tone A, it specifies no guard tone, so none is generated here.
class ToneBGenerator {
public:
    explicit ToneBGenerator(Sample amplitude, int sampleRateHz = kSampleRateHz);

    void GenerateSamples(std::vector<Sample>& out, int sampleCount);
    void ReversePhase();

private:
    ToneWithPhaseReversal tone_; // 1200 Hz
};

} // namespace voipmodem::dsp
