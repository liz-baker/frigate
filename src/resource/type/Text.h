#pragma once

#include "ship/resource/Resource.h"

#include <cstdint>
#include <string>
#include <vector>

// GoldenEye 007's text-bank resource type ("GETX") -- the fixed-layout
// binary format `assets/obseg/text/*` segments carry (frigate issue #11).
// On-disk layout: docs/formats/goldeneye-font-text.md.
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
