#include "DtmfGenerator.h"

#include "Dds.h"

namespace voipmodem::dsp {

namespace {
constexpr double kRow[4] = {697.0, 770.0, 852.0, 941.0};
constexpr double kCol[4] = {1209.0, 1336.0, 1477.0, 1633.0};

void GenerateDualTone(double hz1, double hz2, double durationSeconds, Sample amplitude, std::vector<Sample>& out,
                       int sampleRateHz) {
    Dds low(hz1, sampleRateHz);
    Dds high(hz2, sampleRateHz);
    const int sampleCount = static_cast<int>(durationSeconds * sampleRateHz);
    out.reserve(out.size() + static_cast<size_t>(sampleCount));
    const Sample half = amplitude * 0.5f; // each tone at half amplitude so the sum doesn't clip
    for (int i = 0; i < sampleCount; ++i) {
        out.push_back(half * low.NextSample() + half * high.NextSample());
    }
}
} // namespace

std::optional<std::pair<double, double>> GetDtmfFrequencies(char digit) {
    switch (digit) {
        case '1': return std::make_pair(kRow[0], kCol[0]);
        case '2': return std::make_pair(kRow[0], kCol[1]);
        case '3': return std::make_pair(kRow[0], kCol[2]);
        case 'A': case 'a': return std::make_pair(kRow[0], kCol[3]);
        case '4': return std::make_pair(kRow[1], kCol[0]);
        case '5': return std::make_pair(kRow[1], kCol[1]);
        case '6': return std::make_pair(kRow[1], kCol[2]);
        case 'B': case 'b': return std::make_pair(kRow[1], kCol[3]);
        case '7': return std::make_pair(kRow[2], kCol[0]);
        case '8': return std::make_pair(kRow[2], kCol[1]);
        case '9': return std::make_pair(kRow[2], kCol[2]);
        case 'C': case 'c': return std::make_pair(kRow[2], kCol[3]);
        case '*': return std::make_pair(kRow[3], kCol[0]);
        case '0': return std::make_pair(kRow[3], kCol[1]);
        case '#': return std::make_pair(kRow[3], kCol[2]);
        case 'D': case 'd': return std::make_pair(kRow[3], kCol[3]);
        default: return std::nullopt;
    }
}

void GenerateDtmfDigit(char digit, double durationSeconds, Sample amplitude, std::vector<Sample>& out,
                       int sampleRateHz) {
    const auto freqs = GetDtmfFrequencies(digit);
    if (!freqs.has_value()) {
        return;
    }
    GenerateDualTone(freqs->first, freqs->second, durationSeconds, amplitude, out, sampleRateHz);
}

void GenerateDialTone(double durationSeconds, Sample amplitude, std::vector<Sample>& out, int sampleRateHz) {
    GenerateDualTone(350.0, 440.0, durationSeconds, amplitude, out, sampleRateHz);
}

} // namespace voipmodem::dsp
