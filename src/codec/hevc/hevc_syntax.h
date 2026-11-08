// Spec-faithful HEVC (H.265) VPS/SPS/PPS syntax parser.
//
// Unlike the helper's derived summaries, this follows ITU-T H.265 clause 7.3
// syntax element by element (names, nesting, ue(v)/se(v)/u(n) values) and
// records bit offsets/lengths, so the Syntax panel can show the real syntax
// tree. Only the VPS/SPS/PPS NAL units are parsed here; slice/CTU syntax comes
// from the helper.
#pragma once

#include <cstddef>
#include <cstdint>

#include "common/syntax.h"

namespace vide {

// Scans the Annex-B byte stream in `data`, parses every VPS/SPS/PPS NAL unit,
// and appends one spec structure root per NAL to `tree`. Returns how many were
// parsed.
int parseHevcParameterSets(const uint8_t* data, std::size_t size, SyntaxTree& tree);

}  // namespace vide
