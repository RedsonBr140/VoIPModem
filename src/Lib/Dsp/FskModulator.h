#pragma once

#include <cstdint>
#include <vector>

#include "Dds.h"
#include "DspTypes.h"

namespace voipmodem::dsp {

// Continuous-phase binary FSK modulator at a configurable mark/space
// frequency pair and baud rate. This is the actual signaling layer the
// ITU-T V.8 handshake runs on: V.21 channel 1 (mark 980 Hz / space 1180
// Hz, used by the calling party's transmit direction) and channel 2
// (mark 1650 Hz / space 1850 Hz, used by the answering party's transmit
// direction), both at 300 baud -- confirmed against V.21's own frequency
// table and how V.8 assigns CM (calling party, channel 1) vs. JM
// (answering party, channel 2) to the two channels, not assumed. See
// docs/ROADMAP.md Phase 4.
//
// "Continuous-phase" means switching between mark and space does not
// reset the oscillator's phase -- it only changes frequency, exactly
// like Dds::SetFrequency already behaves. This avoids the sharp phase
// discontinuities a naive "restart a new tone each bit" implementation
// would introduce, which would waste bandwidth on spurious harmonics a
// real FSK demodulator (including GoertzelDetector-based ones) is more
// likely to be confused by.
class FskModulator {
public:
    FskModulator(double markHz, double spaceHz, double baudRate, Sample amplitude,
                 int sampleRateHz = kSampleRateHz);

    // Appends one bit period's worth of samples to `out`: the mark
    // frequency if `bit` is true (binary 1), the space frequency if
    // false (binary 0). The exact number of samples varies by +-1 across
    // calls when sampleRateHz/baudRate isn't a whole number (e.g. 8000/300
    // = 26.67) -- accumulated as a running fractional remainder (the same
    // technique Dds uses for its phase accumulator) so the *average* bit
    // rate over many bits is exact, rather than silently truncating every
    // single bit to the same rounded-down length and drifting the clock.
    void ModulateBit(std::vector<Sample>& out, bool bit);

    // One async-framed byte: a start bit (space/0), 8 data bits (LSB
    // first), then a stop bit (mark/1) -- the framing V.21/V.8 CI/CM/JM
    // octets travel in. No parity bit, matching that framing.
    void ModulateByteAsync(std::vector<Sample>& out, std::uint8_t byte);

    // Rewinds phase and the fractional bit-length remainder. Does not
    // change the configured frequencies/baud rate.
    void Reset();

private:
    Dds dds_;
    double markHz_;
    double spaceHz_;
    Sample amplitude_;
    double exactSamplesPerBit_;
    double samplesPerBitRemainder_ = 0.0;
};

} // namespace voipmodem::dsp
