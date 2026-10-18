#include "ToneGenerator.h"

namespace voipmodem::dsp {

ToneGenerator::ToneGenerator(double frequencyHz, Sample amplitude, int sampleRateHz)
    : dds_(frequencyHz, sampleRateHz), amplitude_(amplitude) {}

void ToneGenerator::Generate(std::vector<Sample>& out, int count) {
    out.reserve(out.size() + static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        out.push_back(amplitude_ * dds_.NextSample());
    }
}

void ToneGenerator::Reset() {
    dds_.Reset();
}

} // namespace voipmodem::dsp
