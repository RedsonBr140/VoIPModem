#pragma once

#include <array>
#include <vector>

#include "Dds.h"
#include "DspTypes.h"

namespace voipmodem::dsp {

// Generates ITU-T V.34's line probing signals L1 and L2 (clause
// 10.1.2.4, Table 17) -- confirmed against the real Recommendation
// V.34 (02/98) text, raw-mode `pdftotext` extraction specifically
// (the layout-mode extraction mangles this table's column alignment
// around 450/1650/1950/2100/2250Hz -- same discrepancy already found
// and resolved this way for this phase's earlier CRC work; raw mode's
// 21-value count matches the clause's own "150Hz to 3750Hz... Tones at
// 900, 1200, 1800, 2400Hz omitted" description exactly, so it's the
// one trusted here).
//
// Both signals are the same 21-tone multitone waveform -- cosines at
// 150Hz spacing from 150Hz to 3750Hz, four frequencies omitted, each
// tone's initial phase fixed at 0 or 180 degrees per Table 17 (chosen,
// in the original design, to keep the summed waveform's peak-to-
// average ratio low) -- differing only in overall level: L1 at 6dB
// above the nominal power level, L2 at the nominal level itself.
// 10.1.2.4 does not spell out how a single-tone "nominal power"
// reference should translate into a 21-tone sum's per-tone amplitude;
// this implementation's own choice, NOT confirmed against a real V.34
// modem, is equal power per tone such that the summed signal's total
// average power matches a single full-scale tone at `nominalAmplitude`
// (i.e. per-tone amplitude = nominalAmplitude / sqrt(21), since the 21
// tones are at distinct frequencies and so don't correlate with each
// other on average).
//
// A second, easy-to-miss implementation detail: `Dds::NextSample()`
// returns sin(phase), but Table 17 specifies cos(2*pi*f*t + theta) --
// so every tone here carries a constant +90-degree `AddPhase()` offset
// on top of Table 17's own 0/180-degree value, to turn the shared sine
// oscillator into the cosine basis the Recommendation actually
// specifies. This matters once anything downstream does coherent
// multi-tone phase/channel analysis against this signal, even though
// it wouldn't show up in a simple magnitude check.
//
// Deliberately scoped to generation only, same split as everything
// else built this phase: this class says nothing about *when* L1/L2
// are sent during Phase 2 (clause 11.2's sequencing, not implemented
// yet) or how a receiver should analyse what comes back to produce
// INFO1a/INFO1c's line-probing results (also not implemented yet --
// see docs/ROADMAP.md).
class LineProbingSignalGenerator {
public:
    // The 21 probing-tone frequencies, Table 17 order (lowest to
    // highest) -- exposed so a future channel-response analyzer can
    // target exactly these bins without redefining them separately.
    static constexpr std::array<double, 21> kToneFrequenciesHz = {
        150.0,  300.0,  450.0,  600.0,  750.0,  1050.0, 1350.0, 1500.0, 1650.0, 1950.0, 2100.0,
        2250.0, 2550.0, 2700.0, 2850.0, 3000.0, 3150.0, 3300.0, 3450.0, 3600.0, 3750.0};

    explicit LineProbingSignalGenerator(Sample nominalAmplitude, int sampleRateHz = kSampleRateHz);

    // Appends `sampleCount` samples of L1 (6dB above nominal power).
    // Says nothing about L1's own 160ms/24-repetition duration --
    // that's a caller/sequencing detail, not enforced here.
    void GenerateL1Samples(std::vector<Sample>& out, int sampleCount);

    // Appends `sampleCount` samples of L2 (at nominal power). Since
    // L1 and L2 are the same underlying periodic waveform, this
    // continues each tone's phase exactly where GenerateL1Samples()
    // left it -- no reset happens between them unless Reset() is
    // called explicitly.
    void GenerateL2Samples(std::vector<Sample>& out, int sampleCount);

    // Rewinds every tone back to its Table-17 initial phase (not to
    // zero -- that would silently lose the 0/180-degree pattern the
    // whole signal depends on).
    void Reset();

private:
    void GenerateSamples(std::vector<Sample>& out, int sampleCount, Sample perToneAmplitude);

    std::vector<Dds> tones_;
    Sample perToneNominalAmplitude_;
};

} // namespace voipmodem::dsp
