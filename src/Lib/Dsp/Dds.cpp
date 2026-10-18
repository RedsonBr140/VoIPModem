#include "Dds.h"

#include <cmath>

namespace voipmodem::dsp {

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

Dds::Dds(double frequencyHz, int sampleRateHz) : sampleRateHz_(sampleRateHz) {
    SetFrequency(frequencyHz);
}

void Dds::SetFrequency(double frequencyHz) {
    phaseIncrement_ = kTwoPi * frequencyHz / static_cast<double>(sampleRateHz_);
}

Sample Dds::NextSample() {
    Sample cosine;
    return NextSample(cosine);
}

Sample Dds::NextSample(Sample& outCosine) {
    const Sample sine = static_cast<Sample>(std::sin(phase_));
    outCosine = static_cast<Sample>(std::cos(phase_));

    phase_ += phaseIncrement_;
    if (phase_ >= kTwoPi) {
        phase_ -= kTwoPi;
    } else if (phase_ < 0.0) {
        phase_ += kTwoPi;
    }

    return sine;
}

void Dds::Reset() {
    phase_ = 0.0;
}

void Dds::AddPhase(double radians) {
    phase_ += radians;
    while (phase_ >= kTwoPi) {
        phase_ -= kTwoPi;
    }
    while (phase_ < 0.0) {
        phase_ += kTwoPi;
    }
}

} // namespace voipmodem::dsp
