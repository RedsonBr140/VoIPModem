#include "V34Scrambler.h"

namespace voipmodem::dsp {

namespace {
void TapsForMode(V34ScramblerMode mode, int& tapNear, int& tapFar) {
    // (7-1) call mode: GPC = 1 + x^-18 + x^-23
    // (7-2) answer mode: GPA = 1 + x^-5 + x^-23
    if (mode == V34ScramblerMode::CallMode) {
        tapNear = 18;
        tapFar = 23;
    } else {
        tapNear = 5;
        tapFar = 23;
    }
}
} // namespace

V34Scrambler::V34Scrambler(V34ScramblerMode mode) { TapsForMode(mode, tapNear_, tapFar_); }

void V34Scrambler::Reset() { history_.fill(false); }

bool V34Scrambler::ScrambleBit(bool dataBit) {
    // history_[0] holds q[k-1], history_[i] holds q[k-1-i], so
    // history_[tap-1] holds q[k-tap].
    const bool q = dataBit ^ history_[static_cast<size_t>(tapNear_ - 1)] ^ history_[static_cast<size_t>(tapFar_ - 1)];
    for (size_t i = history_.size() - 1; i > 0; --i) {
        history_[i] = history_[i - 1];
    }
    history_[0] = q;
    return q;
}

V34Descrambler::V34Descrambler(V34ScramblerMode mode) { TapsForMode(mode, tapNear_, tapFar_); }

void V34Descrambler::Reset() { history_.fill(false); }

bool V34Descrambler::DescrambleBit(bool scrambledBit) {
    const bool d =
        scrambledBit ^ history_[static_cast<size_t>(tapNear_ - 1)] ^ history_[static_cast<size_t>(tapFar_ - 1)];
    // Unlike the scrambler, the history here is built from the
    // RECEIVED (scrambled) stream, not the recovered data -- this is
    // the self-synchronizing property described in the header.
    for (size_t i = history_.size() - 1; i > 0; --i) {
        history_[i] = history_[i - 1];
    }
    history_[0] = scrambledBit;
    return d;
}

} // namespace voipmodem::dsp
