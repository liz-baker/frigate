#pragma once

#include <memory>

#include "ship/resource/Resource.h"
#include "ship/resource/ResourceFactoryBinary.h"

namespace Ship {
class ResourceManager;
}

namespace Frigate {

// Reads a re-emitted GoldenEye display list (payload layout:
// docs/formats/goldeneye-display-list-resource.md) with libultraship's own
// display-list reader, then expands each G_SETTEX texture request from the
// referenced texture resource (texture/TexExpand.h). Registered under
// kDisplayListResourceType; the result is an ordinary Fast::DisplayList.
class ResourceFactoryBinaryDisplayListV0 final : public Ship::ResourceFactoryBinary {
  public:
    explicit ResourceFactoryBinaryDisplayListV0(std::weak_ptr<Ship::ResourceManager> resourceManager);

    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;

  private:
    std::weak_ptr<Ship::ResourceManager> mResourceManager;
};

} // namespace Frigate
