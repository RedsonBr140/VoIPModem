#include "LineProbingSignalGenerator.h"

#include <cmath>

namespace voipmodem::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;

// true == 180-degree initial phase per Table 17; false == 0 degrees.
// Same order as LineProbingSignalGenerator::kToneFrequenciesHz.
constexpr std::array<bool, 21> kToneIsInverted = {false, true,  false, false, false, false, false,
                                                   false, true,  false, false, true,  false, true,
                                                   false, true,  true,  true,  true,  false, false};

// 6dB as a linear amplitude ratio: 10^(6/20).
constexpr double kSixDbGain = 1.9952623149688795;
} // namespace

LineProbingSignalGenerator::LineProbingSignalGenerator(Sample nominalAmplitude, int sampleRateHz)
    : perToneNominalAmplitude_(
          nominalAmplitude /
          static_cast<Sample>(std::sqrt(static_cast<double>(LineProbingSignalGenerator::kToneFrequenciesHz.size())))) {
    tones_.reserve(kToneFrequenciesHz.size());
    for (size_t i = 0; i < kToneFrequenciesHz.size(); ++i) {
        tones_.emplace_back(kToneFrequenciesHz[i], sampleRateHz);
        tones_.back().AddPhase(kPi / 2.0); // sin-basis Dds -> Table 17's cosine basis
        if (kToneIsInverted[i]) {
            tones_.back().AddPhase(kPi);
        }
    }
}

void LineProbingSignalGenerator::GenerateSamples(std::vector<Sample>& out, int sampleCount, Sample perToneAmplitude) {
    out.reserve(out.size() + static_cast<size_t>(sampleCount));
    for (int n = 0; n < sampleCount; ++n) {
        Sample sum = 0.0f;
        for (Dds& tone : tones_) {
            sum += tone.NextSample();
        }
        out.push_back(perToneAmplitude * sum);
    }
}

void LineProbingSignalGenerator::GenerateL1Samples(std::vector<Sample>& out, int sampleCount) {
    GenerateSamples(out, sampleCount, perToneNominalAmplitude_ * static_cast<Sample>(kSixDbGain));
}

void LineProbingSignalGenerator::GenerateL2Samples(std::vector<Sample>& out, int sampleCount) {
    GenerateSamples(out, sampleCount, perToneNominalAmplitude_);
}

void LineProbingSignalGenerator::Reset() {
    for (size_t i = 0; i < tones_.size(); ++i) {
        tones_[i].Reset();
        tones_[i].AddPhase(kPi / 2.0);
        if (kToneIsInverted[i]) {
            tones_[i].AddPhase(kPi);
        }
    }
}

} // namespace voipmodem::dsp
