#include "TonePhaseReversalDetector.h"

#include <cmath>

namespace voipmodem::dsp {

namespace {
// Minimum sliding-window magnitude, relative to the baseline's own
// magnitude, before a dot-product sign is trusted at all -- guards
// against noise/silence chatter once the real tone has dropped out.
constexpr double kMinRelativeMagnitude = 0.05;
} // namespace

TonePhaseReversalDetector::TonePhaseReversalDetector(double toneHz, int sampleRateHz, int windowSamples)
    : referenceDds_(toneHz, sampleRateHz), windowSamples_(windowSamples) {}

std::vector<long long> TonePhaseReversalDetector::ProcessSamples(const std::vector<Sample>& samples) {
    std::vector<long long> reversals;

    for (Sample sample : samples) {
        Sample cosine;
        const Sample sine = referenceDds_.NextSample(cosine);
        const Product product{static_cast<double>(sample) * static_cast<double>(cosine),
                               static_cast<double>(sample) * static_cast<double>(sine)};

        window_.push_back(product);
        slidingI_ += product.i;
        slidingQ_ += product.q;
        ++totalSamplesSeen_;

        if (window_.size() > static_cast<size_t>(windowSamples_)) {
            const Product oldest = window_.front();
            slidingI_ -= oldest.i;
            slidingQ_ -= oldest.q;
            window_.pop_front();
        }

        if (window_.size() < static_cast<size_t>(windowSamples_)) {
            continue; // window not full yet
        }

        if (!hasBaseline_) {
            baselineI_ = slidingI_;
            baselineQ_ = slidingQ_;
            hasBaseline_ = true;
            continue;
        }

        // Dead time: don't re-evaluate until the sliding window has
        // fully flushed past the previous reversal.
        if (sampleOfLastReversal_ >= 0 && totalSamplesSeen_ - sampleOfLastReversal_ < windowSamples_) {
            continue;
        }

        const double magnitude = std::sqrt(slidingI_ * slidingI_ + slidingQ_ * slidingQ_);
        const double baselineMagnitude = std::sqrt(baselineI_ * baselineI_ + baselineQ_ * baselineQ_);
        if (magnitude < kMinRelativeMagnitude * baselineMagnitude) {
            continue; // too weak to trust a sign decision
        }

        const double dot = slidingI_ * baselineI_ + slidingQ_ * baselineQ_;
        if (dot < 0.0) {
            // The window spans [totalSamplesSeen_ - windowSamples_,
            // totalSamplesSeen_); the sign only flips once more than
            // half the window's energy is past the transition, so its
            // midpoint is the best single-sample estimate of where the
            // reversal actually occurred.
            reversals.push_back(totalSamplesSeen_ - windowSamples_ / 2);
            sampleOfLastReversal_ = totalSamplesSeen_;
            baselineI_ = slidingI_;
            baselineQ_ = slidingQ_;
        }
    }

    return reversals;
}

} // namespace voipmodem::dsp
