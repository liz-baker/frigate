#pragma once

#include "ship/resource/Resource.h"
#include "ship/resource/ResourceFactoryBinary.h"

namespace Frigate {

// Binary factory for ModelSkeleton resources (version 0). Register with
// Ship::ResourceLoader::RegisterResourceFactory() using
// kModelSkeletonResourceType (resource/ResourceType.h).
class ResourceFactoryBinaryModelSkeletonV0 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};

} // namespace Frigate
