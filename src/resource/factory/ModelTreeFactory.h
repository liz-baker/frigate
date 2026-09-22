#pragma once

#include "ship/resource/Resource.h"
#include "ship/resource/ResourceFactoryBinary.h"

namespace Frigate {

// Binary factory for ModelTree resources (version 2). Register with
// Ship::ResourceLoader::RegisterResourceFactory() using
// kModelTreeResourceType (resource/ResourceType.h).
class ResourceFactoryBinaryModelTreeV2 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};

} // namespace Frigate
