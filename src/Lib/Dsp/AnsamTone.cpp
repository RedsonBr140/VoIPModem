#include "AnsamTone.h"

namespace voipmodem::dsp {

namespace {
constexpr double kAnsamCarrierHz = 2100.0;
constexpr double kAnsamEnvelopeHz = 15.0;
constexpr double kPhaseReversalIntervalSeconds = 0.450;
} // namespace

AnsamGenerator::AnsamGenerator(float amplitude, int sampleRateHz)
    : sampleRateHz_(sampleRateHz),
      amplitude_(amplitude),
      carrier_(kAnsamCarrierHz, sampleRateHz),
      envelope_(kAnsamEnvelopeHz, sampleRateHz),
      samplesPerPhaseReversal_(static_cast<int>(kPhaseReversalIntervalSeconds * sampleRateHz)) {}

Sample AnsamGenerator::NextSample() {
    Sample carrierSample = carrier_.NextSample();
    if (phaseInverted_) {
        carrierSample = -carrierSample;
    }

    const Sample envelope = static_cast<Sample>(1.0 + 0.2 * envelope_.NextSample());

    ++samplesSincePhaseReversal_;
    if (samplesSincePhaseReversal_ >= samplesPerPhaseReversal_) {
        phaseInverted_ = !phaseInverted_;
        samplesSincePhaseReversal_ = 0;
    }

    return amplitude_ * envelope * carrierSample;
}

void AnsamGenerator::Reset() {
    carrier_.Reset();
    envelope_.Reset();
    samplesSincePhaseReversal_ = 0;
    phaseInverted_ = false;
}

AnsamDetector::AnsamDetector(int sampleRateHz) : detector_(kAnsamCarrierHz, sampleRateHz) {}

void AnsamDetector::Process(Sample sample) {
    detector_.Process(sample);
}

double AnsamDetector::Magnitude(int blockSize) const {
    return detector_.Magnitude(blockSize);
}

void AnsamDetector::Reset() {
    detector_.Reset();
}

} // namespace voipmodem::dsp
