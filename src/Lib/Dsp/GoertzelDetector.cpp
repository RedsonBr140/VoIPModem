#include "GoertzelDetector.h"

#include <cmath>

namespace voipmodem::dsp {

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

GoertzelDetector::GoertzelDetector(double targetFrequencyHz, int sampleRateHz) {
    const double omega = kTwoPi * targetFrequencyHz / static_cast<double>(sampleRateHz);
    coefficient_ = 2.0 * std::cos(omega);
}

void GoertzelDetector::Reset() {
    q1_ = 0.0;
    q2_ = 0.0;
}

void GoertzelDetector::Process(Sample sample) {
    const double q0 = coefficient_ * q1_ - q2_ + static_cast<double>(sample);
    q2_ = q1_;
    q1_ = q0;
}

double GoertzelDetector::Magnitude(int blockSize) const {
    if (blockSize <= 0) {
        return 0.0;
    }

    // Standard Goertzel power formula: |X(k)|^2 = q1^2 + q2^2 - q1*q2*coefficient.
    const double power = q1_ * q1_ + q2_ * q2_ - q1_ * q2_ * coefficient_;
    if (power <= 0.0) {
        return 0.0;
    }

    // |X(k)| for a full-scale sine at the target frequency over N samples
    // is approximately amplitude * N / 2; divide it back out so this
    // returns an amplitude-scale estimate instead of a raw bin magnitude.
    return 2.0 * std::sqrt(power) / static_cast<double>(blockSize);
}

} // namespace voipmodem::dsp
