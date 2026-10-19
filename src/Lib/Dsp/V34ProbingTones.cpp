#include "V34ProbingTones.h"

namespace voipmodem::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kToneAHz = 2400.0;
constexpr double kToneBHz = 1200.0;
constexpr double kGuardToneHz = 1800.0;
// -1 dB as a linear amplitude ratio: 10^(-1/20).
constexpr double kOneDbBelowNominal = 0.89125093813374556;
} // namespace

ToneWithPhaseReversal::ToneWithPhaseReversal(double toneHz, Sample amplitude, int sampleRateHz)
    : dds_(toneHz, sampleRateHz), amplitude_(amplitude) {}

Sample ToneWithPhaseReversal::NextSample() { return amplitude_ * dds_.NextSample(); }

void ToneWithPhaseReversal::GenerateSamples(std::vector<Sample>& out, int sampleCount) {
    out.reserve(out.size() + static_cast<size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i) {
        out.push_back(NextSample());
    }
}

void ToneWithPhaseReversal::ReversePhase() { dds_.AddPhase(kPi); }

ToneAGenerator::ToneAGenerator(Sample nominalAmplitude, int sampleRateHz)
    : tone_(kToneAHz, nominalAmplitude * static_cast<Sample>(kOneDbBelowNominal), sampleRateHz),
      guardToneDds_(kGuardToneHz, sampleRateHz),
      guardAmplitude_(nominalAmplitude) {}

void ToneAGenerator::GenerateSamples(std::vector<Sample>& out, int sampleCount) {
    out.reserve(out.size() + static_cast<size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i) {
        out.push_back(tone_.NextSample() + guardAmplitude_ * guardToneDds_.NextSample());
    }
}

void ToneAGenerator::ReversePhase() { tone_.ReversePhase(); }

ToneBGenerator::ToneBGenerator(Sample amplitude, int sampleRateHz) : tone_(kToneBHz, amplitude, sampleRateHz) {}

void ToneBGenerator::GenerateSamples(std::vector<Sample>& out, int sampleCount) { tone_.GenerateSamples(out, sampleCount); }

void ToneBGenerator::ReversePhase() { tone_.ReversePhase(); }

} // namespace voipmodem::dsp
