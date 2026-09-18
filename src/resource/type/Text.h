#pragma once

#include "ship/resource/Resource.h"

#include <cstdint>
#include <string>
#include <vector>

// GoldenEye 007's text-bank resource type ("GETX") -- the fixed-layout
// binary format `assets/obseg/text/*` segments carry (frigate issue #11).
// Transcribed from external/007's src/game/language.c (`langGet()`'s offset
// resolution logic) rather than copied -- only the bare layout fact is used
// here, in fresh wording/types, per CLAUDE.md's licensing rules for
// external/007.
//
// On-disk layout (big-endian, matching the ROM's native MIPS byte order --
// no runtime byte-swap occurs; langGet() reads the offset table directly):
//
//   offset 0   : kOffsetTableSize (1024) big-endian u32 offsets, one per
//                string slot. langGet() indexes this table with
//                `slotID & 0x3FF` (10 bits => exactly 1024 possible slots).
//   each entry : a nonzero offset is file-relative (from the segment's own
//                start) to a NUL-terminated ASCII string elsewhere in the
//                same segment; an offset of 0 means that slot has no
//                string.
namespace Frigate {

class Text final : public Ship::Resource<std::string> {
  public:
    using Resource::Resource;

    Text();

    std::string* GetPointer() override;
    size_t GetPointerSize() override;

    static constexpr size_t kOffsetTableSize = 1024;

    // Indexed exactly like langGet()'s `slotID & 0x3FF` -- an empty string
    // at index i means that slot's on-disk offset was 0 (no string present).
    std::vector<std::string> Strings;
};

} // namespace Frigate
