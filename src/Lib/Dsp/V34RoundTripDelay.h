#pragma once

#include "DspTypes.h"

namespace voipmodem::dsp {

// Round-trip-delay arithmetic per ITU-T V.34 clauses 11.2.1.1.4 (call
// modem, RTDEc) and 11.2.1.2.4 (answer modem, RTDEa) -- confirmed
// against the real Recommendation text. Both are defined as "the time
// interval between two specific tone-phase-reversal events at the line
// terminals, minus 40ms" -- this module is only that arithmetic, given
// two sample indices (exactly what TonePhaseReversalDetector reports,
// on the caller's own local sample clock, which sees both the
// instant it transmitted its own reversal and the instant it later
// received the other side's). It knows nothing about running the
// actual handshake that produces those two sample indices in the first
// place -- that's clause 11.2's sequencing/timing/retry state machine,
// not implemented yet (see docs/ROADMAP.md).
constexpr double kReversalEchoDelaySeconds = 0.040; // the "40ms" both RTDEc/RTDEa subtract

// Call modem, 11.2.1.1.4: "RTDEc is the time interval between the
// appearance of the Tone B phase reversal at the modem line terminals
// and receiving the second Tone A phase reversal at the line terminals
// minus 40 ms." `ownToneBReversalSample` is when the call modem's own
// transmitted Tone B reversal occurred (11.2.1.1.3); `secondToneAReversalSample`
// is when it then detected Tone A's second reversal (11.2.1.1.4).
double ComputeCallModemRoundTripDelaySeconds(long long ownToneBReversalSample, long long secondToneAReversalSample,
                                              int sampleRateHz = kSampleRateHz);

// Answer modem, 11.2.1.2.4: "RTDEa is the time interval between
// sending the Tone A phase reversal at the line terminals and
// receiving the Tone B phase reversal at the line terminals minus
// 40 ms." `ownToneAReversalSample` is when the answer modem's own
// transmitted Tone A reversal occurred (11.2.1.2.3); `toneBReversalSample`
// is when it then detected Tone B's reversal (11.2.1.2.4).
double ComputeAnswerModemRoundTripDelaySeconds(long long ownToneAReversalSample, long long toneBReversalSample,
                                                int sampleRateHz = kSampleRateHz);

} // namespace voipmodem::dsp
