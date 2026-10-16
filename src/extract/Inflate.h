#pragma once

#include "extract/Error.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Frigate::Extract {

// GoldenEye's "1172" compression: bytes 0x11 0x72, then a raw DEFLATE stream (no zlib wrapper). The "1173"
// variant (0x11 0x73) carries three more header bytes before the stream. `expectedSize`, when nonzero, is
// the inflated size the caller knows; a stream that produces a different length is reported as corrupt.
Error Inflate1172(std::span<const uint8_t> data, std::vector<uint8_t>& out, size_t expectedSize = 0);

// A bare DEFLATE stream with no header.
Error InflateRaw(std::span<const uint8_t> data, std::vector<uint8_t>& out, size_t expectedSize = 0);

// Raw DEFLATE compression (level 0-9), used by the archive writer.
Error DeflateRaw(std::span<const uint8_t> data, std::vector<uint8_t>& out, int level = 6);

uint32_t Crc32(std::span<const uint8_t> data);

} // namespace Frigate::Extract
