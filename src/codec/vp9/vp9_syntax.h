// VP9 bitstream syntax parser (IVF container + uncompressed frame header).
#pragma once

#include <cstddef>

#include "common/syntax.h"
#include "common/syntax_builder.h"

namespace vide {

// Parses an IVF file: the container header followed by each frame's
// uncompressed header. Returns false when the data is not IVF.
bool parseVp9Syntax(const uint8_t* data, std::size_t size, SyntaxBuilder& builder,
                    uint32_t parent);

}  // namespace vide
