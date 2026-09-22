#pragma once

#include <cstdint>
#include <vector>

#include "ship/resource/Resource.h"

// A model's joint table ("GESK"), from the packer's `<model>/skeleton`
// resource. Payload: docs/formats/goldeneye-model-skeleton-resource.md.
namespace Frigate {

struct SkeletonJoint {
    uint16_t nodeType = 0;
    uint16_t mtxA = 0;
    uint16_t mtxB = 0;
};

class ModelSkeleton final : public Ship::Resource<SkeletonJoint> {
  public:
    using Resource::Resource;

    ModelSkeleton();

    SkeletonJoint* GetPointer() override;
    size_t GetPointerSize() override;

    // Indexed by a GEMD group node's JointID.
    std::vector<SkeletonJoint> Joints;
};

} // namespace Frigate
