// Shared helpers for VidE reference-decoder helper executables.
//
// A helper decodes one stream, writes frames into a .vpk cache, and reports
// progress to the parent over stderr using the HelperMessage wire format
// (reference-software logs use stdout and are ignored by the parent).
#pragma once

#include <cstdint>
#include <string>

#include "common/frame.h"
#include "common/syntax.h"
#include "common/types.h"
#include "common/vpk.h"
#include "core/helper_process.h"

namespace vide {

// Emits one HelperMessage to stderr (length-prefixed), then flushes.
void helperEmit(const HelperMessage& message);

// Convenience emitters.
void helperEmitHello(CodecId codec, int width, int height, BitDepth bit_depth);
void helperEmitProgress(uint32_t frames_ready);
void helperEmitReady(uint32_t frame);
void helperEmitDone();
void helperEmitError(const std::string& message);

// Encodes a decoded RGBA image as the cache's reco payload. For planar inputs the
// helper is expected to already provide RGBA (adapters convert to RGBA).
DecodedImage makeRgbaImage(int width, int height, std::vector<uint8_t>&& rgba);

}  // namespace vide
