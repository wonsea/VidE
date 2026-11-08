// PNG chunk syntax parser.
#pragma once

#include <cstddef>

#include "common/syntax.h"
#include "common/syntax_builder.h"

namespace vide {

// Parses the PNG signature and chunk structure (IHDR/PLTE/IDAT/IEND/...).
void parsePngSyntax(const uint8_t* data, std::size_t size, SyntaxBuilder& builder,
                    uint32_t parent);

}  // namespace vide
