#pragma once

#include "extract/DisplayListEmit.h"
#include "extract/Extractor.h"

#include <map>

namespace Frigate::Extract {

// The texture bank and the image packers (docs/formats/goldeneye-texture-bitstream.md and the formats named at
// each function). The first goes in the textures archive; the rest, with the geometry packers, in the geometry
// archive.

// Every image of the bank, decoded, as `textures/<name>` with a `.pal` companion for a paletted image and a `.mip`
// companion for one the game mipmaps. One image is held in memory at a time.
Error PackTextures(StageContext& context, O2rWriter& writer);

// The hit surface types of every image: `textures/hits` (docs/formats/goldeneye-texture-hits.md).
Error PackTextureHits(StageContext& context, O2rWriter& writer);

// The global image table's HUD, file select and effect images as `hud/...` (docs/formats/goldeneye-hud-images.md).
Error PackHudImages(StageContext& context, O2rWriter& writer);

// The sky and water images as `sky/img/<n>` (docs/formats/goldeneye-sky-images.md).
Error PackSkyImages(StageContext& context, O2rWriter& writer);

// The Rareware logo segment's lists, vertices and images as `rarewarelogo/...`
// (docs/formats/goldeneye-display-list-resource.md).
Error PackRarewareLogo(StageContext& context, O2rWriter& writer);

// The gun barrel screen's backdrop and blood stream as `eyeintro/...` (docs/formats/goldeneye-eye-intro-resources.md).
Error PackEyeIntro(StageContext& context, O2rWriter& writer);

// Maps a G_SETTEX texture id to the resource PackTextures wrote for it, reading each image's header once.
class TextureBindings {
  public:
    explicit TextureBindings(const StageContext& context) : mContext(context) {
    }

    Error Lookup(uint32_t textureId, TextureBinding& binding);
    TextureLookup AsLookup() {
        return [this](uint32_t id, TextureBinding& binding) { return Lookup(id, binding); };
    }

  private:
    const StageContext& mContext;
    std::map<uint32_t, TextureBinding> mCache;
};

// The ROM bytes of image `index` of the bank, in this context's region.
Error ReadImageBytes(const StageContext& context, size_t index, std::span<const uint8_t>& bytes);

// Registers the stages above with their archives, in the order the Python drivers write them.
void RegisterTexturePackers(Extractor& extractor);

} // namespace Frigate::Extract
