#include "../src/Lib/Dsp/V34QamModulator.h"
#include "../src/Lib/Dsp/GoertzelDetector.h"
#include "../src/Lib/Dsp/V34Trn.h"
#include "TestFramework.h"

#include <cmath>
#include <vector>

using namespace voipmodem::dsp;

TEST(SquareRootRaisedCosinePulse_IsSymmetric) {
    const double T = 1.0;
    const double beta = 0.3;
    for (double t : {0.07, 0.25, 0.5, 0.9, 1.3, 2.4}) {
        const double positive = SquareRootRaisedCosinePulse(t, T, beta);
        const double negative = SquareRootRaisedCosinePulse(-t, T, beta);
        EXPECT_TRUE(std::abs(positive - negative) < 1e-9);
    }
}

TEST(SquareRootRaisedCosinePulse_PeakIsAtZero) {
    const double T = 1.0;
    const double beta = 0.3;
    const double atZero = SquareRootRaisedCosinePulse(0.0, T, beta);
    EXPECT_TRUE(std::abs(atZero - 1.0) < 1e-9); // normalized so h(0) == 1
    for (double t : {0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 3.0}) {
        EXPECT_TRUE(SquareRootRaisedCosinePulse(t, T, beta) < atZero);
    }
}

// The closed-form SRRC pulse has a removable singularity at
// t = +/- T/(4*beta) (0/0 in the general formula); V34QamModulator.cpp
// handles it with an explicit limit formula. Confirm that limit
// actually agrees with the value approached from nearby points, rather
// than just "doesn't produce NaN".
TEST(SquareRootRaisedCosinePulse_SingularityMatchesNearbyLimit) {
    const double T = 1.0;
    const double beta = 0.25;
    const double singularPoint = T / (4.0 * beta);

    const double atSingularity = SquareRootRaisedCosinePulse(singularPoint, T, beta);
    const double justBefore = SquareRootRaisedCosinePulse(singularPoint - 1e-6, T, beta);
    const double justAfter = SquareRootRaisedCosinePulse(singularPoint + 1e-6, T, beta);

    EXPECT_TRUE(std::isfinite(atSingularity));
    EXPECT_TRUE(std::abs(atSingularity - justBefore) < 1e-4);
    EXPECT_TRUE(std::abs(atSingularity - justAfter) < 1e-4);
}

// The defining property of a root-raised-cosine pulse: self-convolved
// with itself (i.e. what a matched receive filter would produce), the
// result is a raised-cosine pulse with zero crossings at every nonzero
// integer multiple of the symbol period -- the "zero ISI at the symbol
// decision instants" property real adaptive equalizers rely on. Tested
// here via discrete autocorrelation of a finely-sampled pulse, which
// approximates that continuous self-convolution.
TEST(DiscreteAutocorrelation_OfSampledSrrc_HasRaisedCosineZeroCrossings) {
    const double T = 1.0;
    const double beta = 0.25;
    const int samplesPerSymbol = 16;
    const int spanSymbols = 10;
    const double dt = T / samplesPerSymbol;
    const int halfTapCount = spanSymbols * samplesPerSymbol;

    std::vector<double> taps;
    taps.reserve(static_cast<size_t>(2 * halfTapCount + 1));
    for (int i = -halfTapCount; i <= halfTapCount; ++i) {
        taps.push_back(SquareRootRaisedCosinePulse(i * dt, T, beta));
    }

    auto autocorrelationAtLagSamples = [&](int lagSamples) {
        double sum = 0.0;
        for (size_t i = 0; i + static_cast<size_t>(lagSamples) < taps.size(); ++i) {
            sum += taps[i] * taps[i + static_cast<size_t>(lagSamples)];
        }
        return sum;
    };

    const double peak = autocorrelationAtLagSamples(0);
    for (int symbolLag = 1; symbolLag <= 3; ++symbolLag) {
        const double value = autocorrelationAtLagSamples(symbolLag * samplesPerSymbol);
        EXPECT_TRUE(std::abs(value / peak) < 0.02);
    }
}

TEST(Modulate_ProducesExpectedSampleCount) {
    const double symbolRate = 2400.0;
    const int span = 8;
    V34QamModulator modulator(symbolRate, 1800.0, 0.125, span);

    const std::vector<std::pair<int, int>> symbols(50, std::make_pair(1, 1));
    const auto output = modulator.Modulate(symbols, 0.1);

    const double expectedDuration = (symbols.size() + 2.0 * span) / symbolRate;
    const auto expectedSamples = static_cast<size_t>(std::llround(expectedDuration * kSampleRateHz));
    EXPECT_EQ(output.size(), expectedSamples);
}

TEST(Modulate_EmptySymbolSequenceProducesEmptyOutput) {
    V34QamModulator modulator(2400.0, 1800.0);
    EXPECT_EQ(modulator.Modulate({}, 0.5).size(), static_cast<size_t>(0));
}

// Guards against NaN/Inf regressions (e.g. a division-by-zero slipping
// back into the SRRC singularity handling). This deliberately does NOT
// assert any particular numeric bound on the output: a bare SRRC pulse
// (unlike the matched-filtered raised-cosine case) is not zero-ISI, so
// with a low roll-off (slow-decaying sidelobes) and many neighbouring
// symbols summing in, the unscaled output can run well past +/-1 --
// empirically around 12x the `amplitude` parameter for this roll-off
// and span. Picking an `amplitude` that keeps real output inside
// [-1, 1] is the caller's job (see V34QamModulator.h); this test only
// confirms the math itself never produces non-finite output.
TEST(Modulate_OutputIsFinite) {
    V34TrnGenerator trn(V34ScramblerMode::CallMode, /*use16Point=*/true);
    std::vector<std::pair<int, int>> symbols;
    for (int i = 0; i < 200; ++i) {
        symbols.push_back(trn.NextPoint());
    }

    V34QamModulator modulator(2400.0, 1800.0, 0.125, 8);
    const auto output = modulator.Modulate(symbols, 0.3);

    for (Sample s : output) {
        EXPECT_TRUE(std::isfinite(s));
    }
}

// Real V.34 carrier frequencies from Table 2/V.34: 1800 Hz is the high
// carrier for symbol rate 2400. This checks the modulated TRN burst
// actually carries energy concentrated there, rather than being
// broadband noise-like output.
TEST(Modulate_HasEnergyConcentratedNearCarrierFrequency) {
    V34TrnGenerator trn(V34ScramblerMode::CallMode, /*use16Point=*/true);
    std::vector<std::pair<int, int>> symbols;
    for (int i = 0; i < 400; ++i) {
        symbols.push_back(trn.NextPoint());
    }

    const double carrierHz = 1800.0;
    V34QamModulator modulator(2400.0, carrierHz, 0.125, 8);
    const auto output = modulator.Modulate(symbols, 0.3);

    GoertzelDetector atCarrier(carrierHz);
    GoertzelDetector farFromCarrier(300.0);
    for (Sample s : output) {
        atCarrier.Process(s);
        farFromCarrier.Process(s);
    }

    const double carrierMagnitude = atCarrier.Magnitude(static_cast<int>(output.size()));
    const double farMagnitude = farFromCarrier.Magnitude(static_cast<int>(output.size()));

    EXPECT_TRUE(carrierMagnitude > 0.05);
    EXPECT_TRUE(carrierMagnitude > farMagnitude * 3.0);
}
