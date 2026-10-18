#pragma once

#include <cstdint>
#include <vector>

namespace voipmodem::dsp {

// ITU-T V.34 clause 10.1.2.3.2's CRC generator, used by the INFO0/INFO1
// sequences (10.1.2.3.3-10.1.2.3.5): divides the input bit sequence by
// x^16 + x^12 + x^5 + 1 (confirmed against the Recommendation's own
// polynomial statement, not assumed), with the shift register preset to
// all ones (0xFFFF) before shifting the sequence in, per steps 1-2 of
// 10.1.2.3.2.
//
// Implemented as the conventional reflected bit-serial form for this
// exact polynomial/preset (the same family of algorithm as the
// widely-used "CRC-16/X-25" found across many ITU-T/CCITT-era specs).
// Honest caveat: clause 10.1.2.3.2's own Figure 14 is a circuit-diagram
// image (register bit positions 15..0 with feedback taps), not
// machine-readable text, so the exact shift direction/tap wiring was
// inferred from the polynomial and preset value together with that
// common convention, not independently confirmed pixel-by-pixel against
// the figure. Per CLAUDE.md's rule against claiming interoperability
// without a real test: this has NOT been verified against a real V.34
// modem's actual INFO sequences, only against the external, independent
// CRC-16/X-25 standard test vector for "123456789" (see
// V34CrcTests.cpp) -- which confirms the core shift-register math
// matches that well-known poly/preset/reflection exactly, but not
// necessarily V.34's own bit-for-bit wiring if it happens to differ.
//
// Unlike the common CRC-16/X-25 variant, there is no final XOR/
// complement here: 10.1.2.3.2 step 3 says only to output the raw
// shift-register contents after shifting the sequence in, bit 0 being
// the LSB -- nothing about inverting the result afterwards.
//
// `bits` is shifted in the order given (bits[0] first) -- callers pass
// exactly "all of the information bits in a sequence, except the frame
// sync bits, the start bits, and the fill bits" per 10.1.2.3.2, not a
// raw byte buffer or a whole INFO sequence including its framing.
std::uint16_t ComputeV34Crc16(const std::vector<bool>& bits);

} // namespace voipmodem::dsp
