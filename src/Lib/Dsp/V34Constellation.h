#pragma once

#include <array>
#include <utility>
#include <vector>

namespace voipmodem::dsp {

// One quarter of V.34's 1664-point superconstellation (clause 9.1,
// Figure 5) -- confirmed against the real Recommendation V.34 (02/98)
// text. Figure 5 itself is a literal scatter-plot image, not a text
// table, so this could not be read off directly: extracted via
// `pdftotext -table` (which aligns the image's point labels into a
// consistent column grid far better than `-layout` did for this
// specific figure) and then cross-validated against the spec's OWN
// stated ordering rule -- "the point with the smallest magnitude is
// labelled as 0 ... when two or more points have the same magnitude,
// the point with the greatest imaginary component is taken first."
// All 416 points satisfy that rule with zero exceptions, which would
// be astronomically unlikely by chance if the column-to-point mapping
// were wrong. Still NOT independently confirmed pixel-by-pixel against
// the actual figure image, nor against a real V.34 modem -- see
// docs/ROADMAP.md.
//
// kQuarterConstellation[label] gives the (x, y) integer grid
// coordinates for labels 0..415, in the same grid units Figure 5
// itself uses (odd integers, spacing 4). A later scaling/normalization
// step -- not implemented here -- would convert these into actual
// transmitted symbol amplitudes; this module only captures the point
// SET and its labelling, nothing about power/scaling.
extern const std::array<std::pair<int, int>, 416> kQuarterConstellation;

// Clockwise 90-degree rotation: (x, y) -> (y, -x). Clause 9.6.3 (shell
// mapping/trellis encoding, not implemented here) explicitly specifies
// "clockwise" rotations, so this module matches that convention rather
// than leaving it ambiguous.
std::pair<int, int> RotateClockwise90(std::pair<int, int> point);

// Builds the `pointCount`-point 2D signal constellation per 9.1's own
// construction rule: the `pointCount`/4 lowest-labelled (smallest-
// magnitude) points of the quarter-constellation, plus those same
// points rotated 90, 180, and 270 degrees clockwise. `pointCount` must
// be a positive multiple of 4, no greater than 1664 (the full
// superconstellation) -- which specific sizes a real data/symbol rate
// combination actually uses (Table 10, not implemented here) is a
// separate concern from this general construction rule.
std::vector<std::pair<int, int>> BuildConstellation(int pointCount);

} // namespace voipmodem::dsp
