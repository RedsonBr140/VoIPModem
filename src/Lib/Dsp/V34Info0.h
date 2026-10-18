#pragma once

#include <optional>
#include <vector>

namespace voipmodem::dsp {

// Bit-level codec for ITU-T V.34's INFO0 sequence (clause 10.1.2.3.3,
// Table 14/V.34) -- confirmed against the actual ITU-T Recommendation
// V.34 (02/98) text, not guessed from secondary sources (its public PDF
// from itu.int, converted to text via pdftotext, same approach used for
// V.8 -- see docs/ROADMAP.md Phase 4).
//
// INFO0 is exchanged during V.34 start-up Phase 2, right after the V.8
// handshake (CM/JM/CJ) agrees to attempt V.34 -- it's how each side
// states its symbol-rate/power/clock-source capabilities before line
// probing (L1/L2) happens. This module only covers INFO0's own 49-bit
// layout in isolation, the same discipline used for V8Message before
// V8CallMenu/V8Signal/V8Handshake existed:
// - NOT yet done: INFO1a/INFO1c (they carry computed *results* of line
//   probing -- round-trip delay, per-symbol-rate projected data rate,
//   pre-emphasis filter choice -- none of which this codebase can
//   compute yet, since L1/L2 generation/analysis doesn't exist yet
//   either).
// - NOT yet done: the actual DBPSK modulation/demodulation that turns
//   these bits into/from real audio (10.1.2.3.1 -- binary DPSK at
//   600 bit/s, carrier 2400Hz for the answer modem's INFO0a, 1200Hz for
//   the call modem's INFO0c, each with its own guard tone). This module
//   only produces/consumes a flat `std::vector<bool>`, exactly the
//   seam V8Message used before FskModulator/FskDemodulator existed.
// - NOT yet done: wiring into V8Handshake/CallAudioBridge, or the
//   Phase 2 procedure's sequencing/timing/retry rules (11.2).
//
// This project's own capabilities are deliberately represented as
// minimal/honest right now (no symbol rate above the mandatory three,
// no power reduction, no 1664-point constellations) -- per CLAUDE.md's
// rule against claiming unimplemented capabilities, `MakeDefaultLocalInfo0()`
// does not set any of that, regardless of what a real high-end V.34
// modem might advertise.
struct V34Info0 {
    // Bits 12:14 -- optional, non-mandatory symbol rates. This project
    // does not implement any of them yet.
    bool symbolRate2743Supported = false;
    bool symbolRate2800Supported = false;
    bool symbolRate3429Supported = false;

    // Bits 15:18 -- ability to use the low/high carrier frequency at
    // the optional 3000/3200 symbol rates. Note these are about symbol
    // rates 3000 and 3200 specifically (both otherwise mandatory), not
    // the 2743/2800/3429 rates above.
    bool lowCarrier3000Supported = false;
    bool highCarrier3000Supported = false;
    bool lowCarrier3200Supported = false;
    bool highCarrier3200Supported = false;

    // Bit 19 -- per 10.1.2.3.3's own wording, "set to 0 indicates that
    // transmission with a symbol rate of 3429 is disallowed"; this
    // field is true when 3429 is allowed (bit value 1), matching that
    // phrasing directly rather than inverting it.
    bool symbolRate3429Allowed = false;

    bool canReduceTransmitPower = false; // bit 20

    // Bits 21:23 -- integer 0-5: max allowed symbol-rate-step difference
    // between TX/RX directions (0=2400 .. 5=3429 ordering). LSB-first
    // (bit21=LSB), matching this Recommendation's "LSB:MSB" column
    // header convention for every multi-bit field here.
    std::uint8_t maxSymbolRateStepDifference = 0;

    bool cmeModem = false;                        // bit 24
    bool supports1664PointConstellations = false;  // bit 25

    // Bits 26:27 -- integer 0-3: 0=internal, 1=synchronized to receive
    // timing, 2=external, 3=reserved. LSB-first.
    std::uint8_t transmitClockSource = 0;

    // Bit 28 -- set to 1 to acknowledge correct reception of an INFO0
    // frame during error recovery (11.2.2). This codec doesn't implement
    // that recovery procedure yet; always false for now.
    bool acknowledgePriorInfo0 = false;
};

// This project's own honestly-minimal capabilities (see class comment):
// nothing optional supported, internal clock source, no error-recovery
// ack pending.
V34Info0 MakeDefaultLocalInfo0();

// Builds the full 49-bit INFO0 sequence (bits 0:48 per Table 14, bit 0
// transmitted first): fill(0:3)=1111, frame sync(4:11)=01110010 (left-most
// bit first in time), the capability fields (12:28), a CRC(29:44)
// computed per V34Crc.h over exactly bits 12:28 (10.1.2.3.2's "all
// information bits ... except frame sync, start, and fill bits" --
// INFO0 has no distinct "start bits" field of its own, only fill/sync/
// payload/CRC), and a trailing fill(45:48)=1111.
std::vector<bool> EncodeInfo0(const V34Info0& info);

// Validates the frame sync pattern and the CRC before decoding the
// capability fields -- returns nullopt on a mismatch in either (not
// yet distinguished from each other: both just mean "this isn't a
// correctly-received INFO0 sequence"). `bits` must be exactly 49 bits;
// shorter/longer input also returns nullopt rather than guessing.
std::optional<V34Info0> DecodeInfo0(const std::vector<bool>& bits);

} // namespace voipmodem::dsp
