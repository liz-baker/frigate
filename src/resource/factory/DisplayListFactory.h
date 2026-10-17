#pragma once

#include <memory>
#include <optional>

#include "fast/resource/type/Texture.h"
#include "ship/resource/Resource.h"
#include "ship/resource/ResourceFactoryBinary.h"
#include "texture/TexExpand.h"

namespace Ship {
class ConsoleVariable;
class ResourceManager;
} // namespace Ship

namespace Frigate {

// N64 format/size and dimensions for a Fast::Texture, matching how the
// interpreter decodes it; nullopt for a type with no N64 equivalent.
std::optional<TexExpand::ImageInfo> ImageInfoFor(const Fast::Texture& texture);

// ImageInfoFor for the texture resource with this path hash, including a
// paletted image's `<name>.pal` companion; nullopt if it can't be loaded.
// `mipChains` false leaves out the image's `<name>.mip` chain, so it loads as its base level alone.
std::optional<TexExpand::ImageInfo> LoadImageInfo(Ship::ResourceManager& resourceManager, uint64_t hash,
                                                  bool mipChains = true);

// Drops the model and room display lists from the resource cache so that the next ones loaded are expanded again,
// with the settings as they are then (the Mip-Mapped Textures setting is read at expansion). The cache is keyed by
// path hash, as the game loads them; lists already held by a loaded model or stage stay alive until it is freed.
void UnloadExpandedDisplayLists(Ship::ResourceManager& resourceManager);

// Reads a re-emitted GoldenEye display list (payload layout:
// docs/formats/goldeneye-display-list-resource.md) with libultraship's own
// display-list reader, then expands each G_SETTEX texture request from the
// referenced texture resource (texture/TexExpand.h). Registered under
// kDisplayListResourceType; the result is an ordinary Fast::DisplayList.
class ResourceFactoryBinaryDisplayListV0 final : public Ship::ResourceFactoryBinary {
  public:
    // `consoleVariables` supplies the mip chain setting (menu/CVars.h's kMipChainsCVar); without it, chains are used.
    explicit ResourceFactoryBinaryDisplayListV0(std::weak_ptr<Ship::ResourceManager> resourceManager,
                                                std::weak_ptr<Ship::ConsoleVariable> consoleVariables = {});

    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;

  private:
    std::weak_ptr<Ship::ResourceManager> mResourceManager;
    std::weak_ptr<Ship::ConsoleVariable> mConsoleVariables;
};

} // namespace Frigate
