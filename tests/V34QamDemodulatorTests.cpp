#include "../src/Lib/Dsp/V34QamDemodulator.h"
#include "../src/Lib/Dsp/V34QamModulator.h"
#include "../src/Lib/Dsp/V34Trn.h"
#include "TestFramework.h"

#include <cmath>
#include <vector>

using namespace voipmodem::dsp;

namespace {

// Least-squares real-valued gain estimate recovered/original, pooling
// both I and Q components into one scalar fit. There is no phase
// ambiguity to resolve here (see V34QamDemodulator.h) -- only an
// unknown real amplitude scale from the carrier downconversion and the
// matched filter's self-convolution energy.
double EstimateGain(const std::vector<std::pair<double, double>>& recovered,
                     const std::vector<std::pair<int, int>>& original) {
    double numerator = 0.0;
    double denominator = 0.0;
    for (size_t i = 0; i < recovered.size(); ++i) {
        numerator += recovered[i].first * original[i].first + recovered[i].second * original[i].second;
        denominator += static_cast<double>(original[i].first) * original[i].first +
                       static_cast<double>(original[i].second) * original[i].second;
    }
    return numerator / denominator;
}

// Decodes every recovered point to the nearest point in `alphabet`
// (after dividing out `gain`) and returns how many of the interior
// symbols (margin symbols excluded at each end, where the matched
// filter window runs off the edge of the burst) decode to the wrong
// point.
int CountNearestNeighborDecodeErrors(const std::vector<std::pair<double, double>>& recovered,
                                      const std::vector<std::pair<int, int>>& symbols,
                                      const std::vector<std::pair<int, int>>& alphabet, double gain, int margin) {
    int errors = 0;
    for (size_t i = static_cast<size_t>(margin); i + static_cast<size_t>(margin) < symbols.size(); ++i) {
        const double iHat = recovered[i].first / gain;
        const double qHat = recovered[i].second / gain;

        double bestDistanceSquared = 1e18;
        std::pair<int, int> best{};
        for (const auto& point : alphabet) {
            const double di = iHat - point.first;
            const double dq = qHat - point.second;
            const double distanceSquared = di * di + dq * dq;
            if (distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                best = point;
            }
        }
        if (best != symbols[i]) {
            ++errors;
        }
    }
    return errors;
}

std::vector<std::pair<int, int>> DistinctPoints(const std::vector<std::pair<int, int>>& symbols) {
    std::vector<std::pair<int, int>> distinct;
    for (const auto& s : symbols) {
        bool found = false;
        for (const auto& d : distinct) {
            if (d == s) {
                found = true;
                break;
            }
        }
        if (!found) {
            distinct.push_back(s);
        }
    }
    return distinct;
}

} // namespace

// The core loopback claim, at a symbol rate/sample rate ratio with
// comfortable oversampling (400 baud at 8 kHz = 20 samples/symbol):
// modulate a known 16-point TRN burst, demodulate the exact same
// waveform with matching parameters, and every interior symbol decodes
// (nearest-neighbor against the real alphabet, after dividing out one
// unknown real gain) to exactly the point that was transmitted. This is
// a lossless, phase/timing-known loopback (see V34QamDemodulator.h for
// exactly what that does and doesn't prove) -- not a real-link test,
// and NOT run at V.34's actual 2400-baud rate; see
// Demodulate_AtActualV34SymbolRate_HasSevereResidualIsi_KnownLimitation
// below for why.
TEST(Demodulate_RecoversTrnSymbolsFromModulatedWaveform_UpToScale) {
    V34TrnGenerator trn(V34ScramblerMode::CallMode, /*use16Point=*/true);
    std::vector<std::pair<int, int>> symbols;
    for (int i = 0; i < 300; ++i) {
        symbols.push_back(trn.NextPoint());
    }

    const double symbolRate = 400.0;
    const double carrierHz = 1800.0;
    const double rolloff = 0.125;
    const int span = 8;
    const double amplitude = 0.05;

    V34QamModulator modulator(symbolRate, carrierHz, rolloff, span);
    const auto waveform = modulator.Modulate(symbols, amplitude);

    V34QamDemodulator demodulator(symbolRate, carrierHz, rolloff, span);
    const auto recovered = demodulator.Demodulate(waveform, static_cast<int>(symbols.size()));

    EXPECT_EQ(recovered.size(), symbols.size());

    const double gain = EstimateGain(recovered, symbols);
    EXPECT_TRUE(std::isfinite(gain));
    EXPECT_TRUE(gain > 0.0);

    const auto alphabet = DistinctPoints(symbols);
    const int errors = CountNearestNeighborDecodeErrors(recovered, symbols, alphabet, gain, /*margin=*/span);
    EXPECT_EQ(errors, 0);
}

// Real, measured finding, not a bug: at V.34's own actual operating
// point (2400 baud at an 8 kHz sample rate -- the same kSampleRateHz
// every other DSP primitive in this codebase already assumes, per
// DspTypes.h -- only ~3.33 samples per symbol), this demodulator's
// direct closed-form-pulse matched filter produces severe residual
// ISI: the discrete sum "sum_n waveform[n] * h(t_n - t_k)" is too
// coarse a quadrature of the underlying continuous matched-filter
// integral at this sample density, even though the signal itself is
// comfortably within the 8 kHz Nyquist bandwidth (confirmed separately
// -- this is a quadrature/discretization problem, not an aliasing
// problem, and not fixed by widening filterSpanSymbols: tried span=32
// here, four times the default, and the error rate barely moved).
// Confirmed as the root cause, not guessed: the exact same code at 400
// baud (20 samples/symbol, see the test above) decodes perfectly, and
// a quick sweep found the error rate falls off as samples/symbol rises
// -- rate, not logic, is what's broken here.
//
// This means V34QamDemodulator, as implemented, is NOT yet usable to
// decode real V.34-rate symbols from real 8 kHz call audio. A real fix
// needs either a properly designed discrete-time (fractionally-spaced)
// matched filter instead of this direct closed-form evaluation, or an
// interpolation/upsampling step ahead of it -- neither exists yet. This
// test pins the current, honest number so a future change to either
// V34QamDemodulator or this test has to explain why the number moved,
// rather than quietly loosening a tolerance until it passes.
TEST(Demodulate_AtActualV34SymbolRate_HasSevereResidualIsi_KnownLimitation) {
    V34TrnGenerator trn(V34ScramblerMode::CallMode, /*use16Point=*/true);
    std::vector<std::pair<int, int>> symbols;
    for (int i = 0; i < 300; ++i) {
        symbols.push_back(trn.NextPoint());
    }

    const double symbolRate = 2400.0;
    const double carrierHz = 1800.0;
    const double rolloff = 0.125;
    const int span = 8;
    const double amplitude = 0.05;

    V34QamModulator modulator(symbolRate, carrierHz, rolloff, span);
    const auto waveform = modulator.Modulate(symbols, amplitude);

    V34QamDemodulator demodulator(symbolRate, carrierHz, rolloff, span);
    const auto recovered = demodulator.Demodulate(waveform, static_cast<int>(symbols.size()));

    const double gain = EstimateGain(recovered, symbols);
    const auto alphabet = DistinctPoints(symbols);
    const int errors = CountNearestNeighborDecodeErrors(recovered, symbols, alphabet, gain, /*margin=*/span);
    const int total = static_cast<int>(symbols.size()) - 2 * span;

    // Measured on 2026-10-24: 216/284 (~76%) wrong. Asserting "most
    // symbols are still wrong" rather than the exact count/ratio, so
    // this doesn't become a brittle golden-value test, but a change
    // that brings this down to, say, 5% should fail this assertion and
    // prompt updating the comment above, not just the threshold.
    EXPECT_TRUE(errors > total / 2);
}

TEST(Demodulate_ZeroSymbolsProducesEmptyOutput) {
    V34QamDemodulator demodulator(2400.0, 1800.0);
    const std::vector<Sample> waveform(100, 0.0f);
    EXPECT_EQ(demodulator.Demodulate(waveform, 0).size(), static_cast<size_t>(0));
}

// Guards against NaN/Inf regressions in the matched-filter loop itself,
// independent of the round-trip accuracy tests above.
TEST(Demodulate_OutputIsFinite) {
    V34TrnGenerator trn(V34ScramblerMode::AnswerMode, /*use16Point=*/false);
    std::vector<std::pair<int, int>> symbols;
    for (int i = 0; i < 120; ++i) {
        symbols.push_back(trn.NextPoint());
    }

    V34QamModulator modulator(2400.0, 1800.0, 0.125, 8);
    const auto waveform = modulator.Modulate(symbols, 0.2);

    V34QamDemodulator demodulator(2400.0, 1800.0, 0.125, 8);
    const auto recovered = demodulator.Demodulate(waveform, static_cast<int>(symbols.size()));

    for (const auto& iq : recovered) {
        EXPECT_TRUE(std::isfinite(iq.first));
        EXPECT_TRUE(std::isfinite(iq.second));
    }
}
