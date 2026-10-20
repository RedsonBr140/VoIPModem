#pragma once

#include <array>

namespace voipmodem::dsp {

// ITU-T V.34 (02/98) clause 7, verbatim:
//
//   "A self-synchronizing scrambler shall be included in the modem for
//   the primary channel data. Auxiliary channel data is not scrambled.
//   Each transmission direction uses a different scrambler. According
//   to the direction of transmission, the generating polynomial is:
//
//     Call mode modem generating polynomial: (GPC) = 1 + x-18 + x-23  (7-1)
//     Answer mode modem generating polynomial: (GPA) = 1 + x-5 + x-23 (7-2)
//
//   At the transmitter, the scrambler shall effectively divide the
//   primary channel data sequence by the generating polynomial. The
//   coefficients of the quotients of this division, taken in descending
//   order, form the data sequence which shall appear at the output of
//   the scrambler."
//
// GF(2) division of data sequence d by G(x) = 1 + x^-n1 + x^-n2 produces
// a quotient sequence q satisfying d[k] = q[k] XOR q[k-n1] XOR q[k-n2],
// i.e. the scrambler computes q[k] = d[k] XOR q[k-n1] XOR q[k-n2] --
// feeding back its OWN already-emitted output. The descrambler inverts
// this by multiplying by G(x): d[k] = q[k] XOR q[k-n1] XOR q[k-n2],
// feeding back the RECEIVED (scrambled) bit stream instead of the
// recovered data. That distinction is what makes it self-synchronizing:
// the descrambler's history is built purely from what it has received,
// so after at most max(n1, n2) bits it produces correct output even if
// it started with an arbitrary (wrong) initial history -- no shared
// initial state between scrambler and descrambler is required. See
// V34ScramblerTests.cpp for a test that exercises exactly this property.
enum class V34ScramblerMode { CallMode, AnswerMode };

namespace detail {
constexpr int kScramblerHistoryDepth = 23; // max(n1, n2) across both modes
}

class V34Scrambler {
public:
    explicit V34Scrambler(V34ScramblerMode mode);

    // Clears the internal history to all zero. Per clause 10.1.3.8:
    // "The scrambler is initialized to zero prior to transmission of
    // the TRN signal."
    void Reset();

    bool ScrambleBit(bool dataBit);

private:
    int tapNear_;
    int tapFar_;
    std::array<bool, detail::kScramblerHistoryDepth> history_{};
};

class V34Descrambler {
public:
    explicit V34Descrambler(V34ScramblerMode mode);

    void Reset();

    bool DescrambleBit(bool scrambledBit);

private:
    int tapNear_;
    int tapFar_;
    std::array<bool, detail::kScramblerHistoryDepth> history_{};
};

} // namespace voipmodem::dsp
