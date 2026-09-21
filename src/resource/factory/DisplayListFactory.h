#pragma once

#include <memory>
#include <optional>

#include "fast/resource/type/Texture.h"
#include "ship/resource/Resource.h"
#include "ship/resource/ResourceFactoryBinary.h"
#include "texture/TexExpand.h"

namespace Ship {
class ResourceManager;
}

namespace Frigate {

// N64 format/size and dimensions for a Fast::Texture, matching how the
// interpreter decodes it; nullopt for a type with no N64 equivalent.
std::optional<TexExpand::ImageInfo> ImageInfoFor(const Fast::Texture& texture);

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
