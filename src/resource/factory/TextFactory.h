#pragma once

#include "ship/resource/Resource.h"
#include "ship/resource/ResourceFactoryBinary.h"

namespace Frigate {

// Binary factory for Text resources (version 0) -- deserializes a raw
// assets/obseg/text/* segment (see resource/type/Text.h for the exact
// layout) into a Text resource. Not auto-registered by libultraship-wiiu;
// register with Ship::ResourceLoader::RegisterResourceFactory() using
// kTextResourceType (resource/ResourceType.h).
class ResourceFactoryBinaryTextV0 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};

} // namespace Frigate
