#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "DspTypes.h"

namespace voipmodem::dsp {

// DTMF (dual-tone multi-frequency) digit generation and the classic
// North American dial tone (350+440 Hz continuous) -- purely cosmetic
// local audio cues for a human listening via `WindowsAudioOutput`
// while a call is being set up over SIP, not anything sent over the
// actual call signaling or media: this codebase already dials by
// putting the number straight into the SIP INVITE
// (`ICallController::Dial`), so nothing here needs to reach the far
// end or affect real call routing. Added because there's no physical
// analog line to produce a real dial tone/DTMF sidetone the way a
// hardware modem's own speaker would -- see docs/ROADMAP.md Phase 4.
//
// Standard DTMF frequency pairs (ITU-T Q.23/Bell System 503, well
// established, not specific to any one Recommendation this project is
// otherwise tracking): row (low group) 697/770/852/941 Hz, column
// (high group) 1209/1336/1477/1633 Hz.
std::optional<std::pair<double, double>> GetDtmfFrequencies(char digit);

// Appends `durationSeconds` of `digit`'s DTMF tone (both frequencies
// summed, each at half `amplitude` so the sum doesn't clip) to `out`.
// No-op if `digit` isn't a valid DTMF character (0-9, *, #, A-D).
void GenerateDtmfDigit(char digit, double durationSeconds, Sample amplitude, std::vector<Sample>& out,
                       int sampleRateHz = kSampleRateHz);

// Appends `durationSeconds` of the classic North American dial tone
// (350 Hz + 440 Hz, continuous, each at half `amplitude`) to `out`.
void GenerateDialTone(double durationSeconds, Sample amplitude, std::vector<Sample>& out,
                      int sampleRateHz = kSampleRateHz);

} // namespace voipmodem::dsp
