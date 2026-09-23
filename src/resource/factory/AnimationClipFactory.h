#pragma once

#include "ship/resource/Resource.h"
#include "ship/resource/ResourceFactoryBinary.h"

namespace Frigate {

// Binary factory for AnimationClip resources (version 1). Register with
// Ship::ResourceLoader::RegisterResourceFactory() using
// kAnimationClipResourceType (resource/ResourceType.h).
class ResourceFactoryBinaryAnimationClipV1 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};

} // namespace Frigate
