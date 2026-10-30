#pragma once

#include "extract/Error.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frigate::Extract {

// Decodes a stage's stan file (its collision tiles) into the GEST resource payload. Layout of both:
// docs/formats/goldeneye-stan-resource.md. The file links its tiles by byte offset; the payload links them by tile
// index, so the runtime never does pointer arithmetic on the file.
Error ConvertStanFile(std::span<const uint8_t> data, std::vector<uint8_t>& payload);

} // namespace Frigate::Extract
