#include "FskDemodulator.h"

#include <algorithm>
#include <cmath>

namespace voipmodem::dsp {

namespace {
// Early-late gate shift, as a fraction of one bit period. Big enough
// that the early/late windows actually land on measurably different
// signal content; small enough that both still mostly overlap the true
// bit rather than the neighbouring one.
constexpr double kTimingGateFraction = 1.0 / 6.0;
// PI loop filter gains (see the class/member comments). The
// proportional term is deliberately conservative -- converge over
// several bits, don't snap on one noisy measurement. The integral gain
// is smaller still: it's accumulating a persistent rate correction, so
// a gain large enough to track real drift quickly but not large enough
// to chase per-bit measurement noise.
constexpr double kProportionalGain = 0.25;
constexpr double kIntegralGain = 0.05;
constexpr double kMaxProportionalNudgePerBit = 1.0;
constexpr double kMaxIntegralRateSamplesPerBit = 2.0; // generous vs. any realistic jitter-buffer rate error
} // namespace

FskDemodulator::FskDemodulator(double markHz, double spaceHz, double baudRate, int sampleRateHz)
    : markHz_(markHz),
      spaceHz_(spaceHz),
      sampleRateHz_(sampleRateHz),
      exactSamplesPerBit_(static_cast<double>(sampleRateHz) / baudRate),
      markDetector_(markHz, sampleRateHz),
      spaceDetector_(spaceHz, sampleRateHz),
      timingDelta_(std::max(1, static_cast<int>(std::lround(exactSamplesPerBit_ * kTimingGateFraction)))) {}

void FskDemodulator::RecordHistory(Sample sample) {
    sampleHistory_.push_back(sample);
    ++totalSamplesSeen_;
    const size_t capacity =
        static_cast<size_t>(std::ceil(exactSamplesPerBit_)) + 2 * static_cast<size_t>(timingDelta_) + 8;
    while (sampleHistory_.size() > capacity) {
        sampleHistory_.pop_front();
    }
}

void FskDemodulator::MaybeApplyPendingTimingCorrection() {
    if (!hasPendingCorrection_) {
        return;
    }
    const long long lateWindowEnd = pendingBitStartAbs_ + pendingBitLen_ + timingDelta_;
    if (totalSamplesSeen_ < lateWindowEnd) {
        return; // not enough future samples yet -- try again once more arrive
    }
    hasPendingCorrection_ = false; // either applied now, or the window's gone -- don't retry stale state

    const long long historyStartAbs = totalSamplesSeen_ - static_cast<long long>(sampleHistory_.size());
    const long long earlyStart = pendingBitStartAbs_ - timingDelta_;
    const long long lateStart = pendingBitStartAbs_ + timingDelta_;
    const long long earlyFirstIndex = earlyStart - historyStartAbs;
    const long long lateLastIndex = lateStart - historyStartAbs + pendingBitLen_ - 1;
    if (earlyFirstIndex < 0 || lateLastIndex >= static_cast<long long>(sampleHistory_.size())) {
        return; // window fell outside retained history -- skip correcting on this bit
    }

    GoertzelDetector earlyDetector(pendingBitWasMark_ ? markHz_ : spaceHz_, sampleRateHz_);
    GoertzelDetector lateDetector(pendingBitWasMark_ ? markHz_ : spaceHz_, sampleRateHz_);
    for (int i = 0; i < pendingBitLen_; ++i) {
        earlyDetector.Process(sampleHistory_[static_cast<size_t>(earlyFirstIndex + i)]);
        lateDetector.Process(sampleHistory_[static_cast<size_t>(lateStart - historyStartAbs + i)]);
    }

    const double earlyMag = earlyDetector.Magnitude(pendingBitLen_);
    const double lateMag = lateDetector.Magnitude(pendingBitLen_);
    // >0: the target tone has more energy later than where we sampled
    // it -- the true edge is later than assumed, so push future
    // windows later (and vice versa).
    const double error = lateMag - earlyMag;
    const double proportionalNudge =
        std::clamp(kProportionalGain * error, -kMaxProportionalNudgePerBit, kMaxProportionalNudgePerBit);
    bitLengthRemainder_ += proportionalNudge;
    estimatedRateErrorSamplesPerBit_ = std::clamp(estimatedRateErrorSamplesPerBit_ + kIntegralGain * error,
                                                   -kMaxIntegralRateSamplesPerBit, kMaxIntegralRateSamplesPerBit);
}

std::vector<bool> FskDemodulator::ProcessSamples(const std::vector<Sample>& samples) {
    std::vector<bool> bits;

    for (Sample sample : samples) {
        if (currentBitTargetLength_ < 0) {
            bitLengthRemainder_ += exactSamplesPerBit_ + estimatedRateErrorSamplesPerBit_;
            currentBitTargetLength_ = static_cast<int>(std::llround(bitLengthRemainder_));
            bitLengthRemainder_ -= currentBitTargetLength_;
            markDetector_.Reset();
            spaceDetector_.Reset();
            samplesIntoCurrentBit_ = 0;
        }

        RecordHistory(sample);
        markDetector_.Process(sample);
        spaceDetector_.Process(sample);
        ++samplesIntoCurrentBit_;

        if (samplesIntoCurrentBit_ >= currentBitTargetLength_) {
            const bool decidedMark =
                markDetector_.Magnitude(currentBitTargetLength_) > spaceDetector_.Magnitude(currentBitTargetLength_);
            bits.push_back(decidedMark);

            hasPendingCorrection_ = true;
            pendingBitStartAbs_ = totalSamplesSeen_ - currentBitTargetLength_;
            pendingBitLen_ = currentBitTargetLength_;
            pendingBitWasMark_ = decidedMark;

            currentBitTargetLength_ = -1; // next sample starts a new bit
        }

        MaybeApplyPendingTimingCorrection();
    }

    return bits;
}

std::optional<std::uint8_t> FskDemodulator::TryDecodeAsyncByte(const std::vector<bool>& bits, size_t& startIndex) {
    constexpr size_t kFrameBits = 10; // 1 start + 8 data + 1 stop
    if (startIndex + kFrameBits > bits.size()) {
        return std::nullopt;
    }

    const bool startBit = bits[startIndex];
    const bool stopBit = bits[startIndex + 9];
    if (startBit != false || stopBit != true) {
        return std::nullopt; // not actually framed here -- caller wasn't aligned, not a corrupt byte
    }

    std::uint8_t byte = 0;
    for (int i = 0; i < 8; ++i) {
        if (bits[startIndex + 1 + i]) {
            byte |= static_cast<std::uint8_t>(1u << i); // LSB first
        }
    }

    startIndex += kFrameBits;
    return byte;
}

} // namespace voipmodem::dsp
