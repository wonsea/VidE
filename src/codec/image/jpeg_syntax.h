// JPEG marker-segment syntax parser.
#pragma once

#include <cstddef>

#include "common/syntax.h"
#include "common/syntax_builder.h"

namespace vide {

// Parses the marker structure of a JPEG file (stops at SOS, since entropy-coded
// scan data has no byte-level syntax).
void parseJpegSyntax(const uint8_t* data, std::size_t size, SyntaxBuilder& builder,
                     uint32_t parent);

}  // namespace vide
