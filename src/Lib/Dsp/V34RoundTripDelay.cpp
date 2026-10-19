#include "V34RoundTripDelay.h"

namespace voipmodem::dsp {

double ComputeCallModemRoundTripDelaySeconds(long long ownToneBReversalSample, long long secondToneAReversalSample,
                                              int sampleRateHz) {
    const double intervalSeconds =
        static_cast<double>(secondToneAReversalSample - ownToneBReversalSample) / static_cast<double>(sampleRateHz);
    return intervalSeconds - kReversalEchoDelaySeconds;
}

double ComputeAnswerModemRoundTripDelaySeconds(long long ownToneAReversalSample, long long toneBReversalSample,
                                                int sampleRateHz) {
    const double intervalSeconds =
        static_cast<double>(toneBReversalSample - ownToneAReversalSample) / static_cast<double>(sampleRateHz);
    return intervalSeconds - kReversalEchoDelaySeconds;
}

} // namespace voipmodem::dsp
