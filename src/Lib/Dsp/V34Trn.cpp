#include "V34Trn.h"

#include "V34Constellation.h"

namespace voipmodem::dsp {

namespace {
std::pair<int, int> RotateClockwiseSteps(std::pair<int, int> point, int steps) {
    for (int i = 0; i < steps; ++i) {
        point = RotateClockwise90(point);
    }
    return point;
}
} // namespace

V34TrnGenerator::V34TrnGenerator(V34ScramblerMode mode, bool use16Point)
    : scrambler_(mode), use16Point_(use16Point) {
    Reset();
}

void V34TrnGenerator::Reset() { scrambler_.Reset(); }

std::pair<int, int> V34TrnGenerator::NextPoint() {
    const bool i1 = scrambler_.ScrambleBit(true);
    const bool i2 = scrambler_.ScrambleBit(true);
    const int inValue = (i2 ? 2 : 0) + (i1 ? 1 : 0);

    if (!use16Point_) {
        return RotateClockwiseSteps(kQuarterConstellation[0], inValue);
    }

    const bool q1 = scrambler_.ScrambleBit(true);
    const bool q2 = scrambler_.ScrambleBit(true);
    const int baseIndex = (q2 ? 2 : 0) + (q1 ? 1 : 0);
    return RotateClockwiseSteps(kQuarterConstellation[static_cast<size_t>(baseIndex)], inValue);
}

} // namespace voipmodem::dsp
