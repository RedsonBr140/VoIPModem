#include "../src/Lib/Dsp/V34RoundTripDelay.h"
#include "TestFramework.h"

using namespace voipmodem::dsp;

namespace {
bool NearlyEqual(double a, double b, double tolerance = 1e-9) { return (a > b ? a - b : b - a) <= tolerance; }
} // namespace

TEST(ComputeCallModemRoundTripDelaySeconds_SubtractsThe40msEchoDelay) {
    // 11.2.1.1.3 inserts exactly a 40ms delay between receiving Tone
    // A's first reversal and sending Tone B's reversal; if the real
    // round trip were otherwise zero, the raw interval measured here
    // would read exactly 40ms, and RTDEc must come out to zero.
    const long long ownToneBReversalSample = 0;
    const long long secondToneAReversalSample = static_cast<long long>(0.040 * kSampleRateHz); // 320 @ 8kHz

    const double rtd =
        ComputeCallModemRoundTripDelaySeconds(ownToneBReversalSample, secondToneAReversalSample, kSampleRateHz);

    EXPECT_TRUE(NearlyEqual(rtd, 0.0, 1e-6));
}

TEST(ComputeCallModemRoundTripDelaySeconds_NonZeroRoundTrip) {
    // A real 20ms round trip on top of the mandatory 40ms echo delay.
    const long long ownToneBReversalSample = 1000;
    const double intervalSeconds = 0.040 + 0.020;
    const long long secondToneAReversalSample =
        ownToneBReversalSample + static_cast<long long>(intervalSeconds * kSampleRateHz);

    const double rtd =
        ComputeCallModemRoundTripDelaySeconds(ownToneBReversalSample, secondToneAReversalSample, kSampleRateHz);

    EXPECT_TRUE(NearlyEqual(rtd, 0.020, 1e-6));
}

TEST(ComputeAnswerModemRoundTripDelaySeconds_SubtractsThe40msEchoDelay) {
    const long long ownToneAReversalSample = 500;
    const long long toneBReversalSample = ownToneAReversalSample + static_cast<long long>(0.040 * kSampleRateHz);

    const double rtd = ComputeAnswerModemRoundTripDelaySeconds(ownToneAReversalSample, toneBReversalSample, kSampleRateHz);

    EXPECT_TRUE(NearlyEqual(rtd, 0.0, 1e-6));
}

TEST(ComputeAnswerModemRoundTripDelaySeconds_NonZeroRoundTrip) {
    const long long ownToneAReversalSample = 0;
    const double intervalSeconds = 0.040 + 0.0125;
    const long long toneBReversalSample = static_cast<long long>(intervalSeconds * kSampleRateHz);

    const double rtd = ComputeAnswerModemRoundTripDelaySeconds(ownToneAReversalSample, toneBReversalSample, kSampleRateHz);

    EXPECT_TRUE(NearlyEqual(rtd, 0.0125, 1e-6));
}
