// Serialises a SyntaxTree to JSON for the `--dump-syntax` CLI option.
#pragma once

#include <string>

#include "common/syntax.h"

namespace vide {

std::string syntaxTreeToJson(const SyntaxTree& tree);

}  // namespace vide
