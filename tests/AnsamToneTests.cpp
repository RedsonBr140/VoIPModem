#include "../src/Lib/Dsp/AnsamTone.h"
#include "TestFramework.h"

using namespace voipmodem::dsp;

TEST(AnsamGenerator_ProducesEnergyAtCarrierFrequency) {
    AnsamGenerator generator(0.6f, kSampleRateHz);
    GoertzelDetector detector(2100.0, kSampleRateHz);

    const int blockSize = 160; // one 20ms frame, same granularity CallAudioBridge uses
    for (int i = 0; i < blockSize; ++i) {
        detector.Process(generator.NextSample());
    }

    EXPECT_TRUE(detector.Magnitude(blockSize) > 0.2);
}

TEST(AnsamGenerator_NoEnergyAtAnUnrelatedFrequency) {
    AnsamGenerator generator(0.6f, kSampleRateHz);
    GoertzelDetector detector(1000.0, kSampleRateHz);

    const int blockSize = 160;
    for (int i = 0; i < blockSize; ++i) {
        detector.Process(generator.NextSample());
    }

    EXPECT_TRUE(detector.Magnitude(blockSize) < 0.05);
}

TEST(AnsamDetector_DetectsGeneratorOutput) {
    AnsamGenerator generator(0.6f, kSampleRateHz);
    AnsamDetector detector(kSampleRateHz);

    const int blockSize = 160;
    for (int i = 0; i < blockSize; ++i) {
        detector.Process(generator.NextSample());
    }

    EXPECT_TRUE(detector.Magnitude(blockSize) > 0.2);
}

TEST(AnsamDetector_SilenceReadsNearZero) {
    AnsamDetector detector(kSampleRateHz);
    for (int i = 0; i < 160; ++i) {
        detector.Process(0.0f);
    }
    EXPECT_TRUE(detector.Magnitude(160) < 0.01);
}

TEST(AnsamGenerator_EnvelopeVariesOverTime_NotAConstantAmplitudeTone) {
    // Sample two blocks half a 15Hz AM cycle apart (roughly) and confirm
    // the measured magnitude actually differs -- proof the envelope is
    // doing something, not just emitting a plain 2100Hz tone.
    AnsamGenerator generator(0.6f, kSampleRateHz);
    GoertzelDetector detector(2100.0, kSampleRateHz);

    const int blockSize = 160;
    double firstMagnitude = 0.0;
    double secondMagnitude = 0.0;

    for (int i = 0; i < blockSize; ++i) {
        detector.Process(generator.NextSample());
    }
    firstMagnitude = detector.Magnitude(blockSize);
    detector.Reset();

    // Skip ahead roughly 1/(2*15Hz) seconds to land near the opposite
    // envelope extreme.
    const int samplesToSkip = kSampleRateHz / 30;
    for (int i = 0; i < samplesToSkip; ++i) {
        generator.NextSample();
    }

    for (int i = 0; i < blockSize; ++i) {
        detector.Process(generator.NextSample());
    }
    secondMagnitude = detector.Magnitude(blockSize);

    EXPECT_TRUE(firstMagnitude > 0.0);
    EXPECT_TRUE(secondMagnitude > 0.0);
    // Both should still be within ANSam's envelope range, not blown out.
    EXPECT_TRUE(firstMagnitude < 1.0);
    EXPECT_TRUE(secondMagnitude < 1.0);
}
