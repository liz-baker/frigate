#pragma once

#include <cstdint>
#include <vector>

#include "ship/resource/Resource.h"

// A packed animation clip ("GEAN"), from the packer's `animations/<name>`
// resource. Payload: docs/formats/goldeneye-animation-clip-resource.md.
namespace Frigate {

class AnimationClip final : public Ship::Resource<uint8_t> {
  public:
    using Resource::Resource;

    AnimationClip();

    uint8_t* GetPointer() override;
    size_t GetPointerSize() override;

    uint32_t FrameCount = 0;
    uint8_t AngleWidthBits = 0;
    bool Loop = false;
    // Bytes to advance between frames in Bitstream.
    uint32_t FrameSizeBytes = 0;
    std::vector<uint8_t> Bitstream;
};

} // namespace Frigate
