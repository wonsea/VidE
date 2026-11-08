// Spec-faithful VVC (H.266) VPS/SPS/PPS syntax parser.
//
// Follows ITU-T H.266 clause 7.3 for the common (single-layer, Main-profile)
// path: exact syntax element names, nesting and bit offsets. Uncommon branches
// (explicit scaling lists, many reference-picture-list details) are marked as
// unparsed and stop cleanly rather than desynchronising the bit reader.
#pragma once

#include <cstddef>
#include <cstdint>

#include "common/syntax.h"

namespace vide {

// Scans the Annex-B byte stream in `data`, parses every VPS/SPS/PPS NAL unit and
// appends one structure root per NAL to `tree`. Returns how many were parsed.
int parseVvcParameterSets(const uint8_t* data, std::size_t size, SyntaxTree& tree);

}  // namespace vide
