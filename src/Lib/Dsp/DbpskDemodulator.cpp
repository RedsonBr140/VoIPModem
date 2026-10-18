#include "DbpskDemodulator.h"

#include <cmath>

namespace voipmodem::dsp {

DbpskDemodulator::DbpskDemodulator(double carrierHz, double baudRate, int sampleRateHz)
    : dds_(carrierHz, sampleRateHz), exactSamplesPerSymbol_(static_cast<double>(sampleRateHz) / baudRate) {}

std::vector<bool> DbpskDemodulator::ProcessSamples(const std::vector<Sample>& samples) {
    std::vector<bool> bits;

    for (Sample sample : samples) {
        if (currentSymbolTargetLength_ < 0) {
            samplesPerSymbolRemainder_ += exactSamplesPerSymbol_;
            currentSymbolTargetLength_ = static_cast<int>(std::llround(samplesPerSymbolRemainder_));
            samplesPerSymbolRemainder_ -= currentSymbolTargetLength_;
            iAccum_ = 0.0;
            qAccum_ = 0.0;
            samplesIntoCurrentSymbol_ = 0;
        }

        Sample cosine;
        const Sample sine = dds_.NextSample(cosine);
        iAccum_ += static_cast<double>(sample) * static_cast<double>(cosine);
        qAccum_ += static_cast<double>(sample) * static_cast<double>(sine);
        ++samplesIntoCurrentSymbol_;

        if (samplesIntoCurrentSymbol_ >= currentSymbolTargetLength_) {
            if (hasPreviousSymbol_) {
                // Real part of (I, Q) * conjugate(previousI, previousQ):
                // negative means the phase rotated ~180 degrees since
                // the previous symbol -- a 1 bit per 10.1.2.3.1.
                const double dot = iAccum_ * previousI_ + qAccum_ * previousQ_;
                bits.push_back(dot < 0.0);
            } else {
                hasPreviousSymbol_ = true; // this was the arbitrary-phase reference symbol
            }
            previousI_ = iAccum_;
            previousQ_ = qAccum_;
            currentSymbolTargetLength_ = -1; // next sample starts a new symbol
        }
    }

    return bits;
}

} // namespace voipmodem::dsp
