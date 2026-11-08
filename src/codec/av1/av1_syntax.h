// AV1 bitstream syntax parser (OBU structure + libaom sequence-header fields).
#pragma once

#include <cstddef>

#include "common/syntax.h"
#include "common/syntax_builder.h"

namespace vide {

// Parses a low-overhead AV1 OBU stream: lists its OBUs and fills in the
// sequence-header fields. When libaom is available its bitstream inspector is
// used for the header values, otherwise the fields are left as unparsed.
void parseAv1Syntax(const uint8_t* data, std::size_t size, SyntaxBuilder& builder,
                    uint32_t parent);

}  // namespace vide
