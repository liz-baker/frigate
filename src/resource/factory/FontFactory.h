#pragma once

#include "ship/resource/Resource.h"
#include "ship/resource/ResourceFactoryBinary.h"

namespace Frigate {

// Binary factory for Font resources (version 0) -- deserializes a raw
// assets/font/*.bin segment (see resource/type/Font.h for the exact layout)
// into a Font resource. Not auto-registered by libultraship; register
// with Ship::ResourceLoader::RegisterResourceFactory() using
// kFontResourceType (resource/ResourceType.h).
class ResourceFactoryBinaryFontV0 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};

} // namespace Frigate
