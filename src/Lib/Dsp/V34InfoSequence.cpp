#include "V34InfoSequence.h"

#include "DbpskDemodulator.h"
#include "DbpskModulator.h"

namespace voipmodem::dsp {

namespace {
constexpr Sample kInfoAmplitude = 0.8f;
} // namespace

void EncodeInfo0ToSamples(const V34Info0& info, double carrierHz, std::vector<Sample>& out, int sampleRateHz) {
    const std::vector<bool> bits = EncodeInfo0(info);

    DbpskModulator modulator(carrierHz, kInfoBaudRate, kInfoAmplitude, sampleRateHz);
    modulator.EmitReferenceSymbol(out);
    for (bool bit : bits) {
        modulator.ModulateBit(out, bit);
    }
}

std::optional<V34Info0> DecodeInfo0FromSamples(const std::vector<Sample>& samples, double carrierHz,
                                                int sampleRateHz) {
    DbpskDemodulator demodulator(carrierHz, kInfoBaudRate, sampleRateHz);
    const std::vector<bool> bits = demodulator.ProcessSamples(samples);
    return DecodeInfo0(bits);
}

} // namespace voipmodem::dsp
