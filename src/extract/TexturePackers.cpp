#include "extract/TexturePackers.h"

namespace Frigate::Extract {

// The textures archive is build_textures_o2r.py's; the rest are build_geometry_o2r.py's, in its order (the model,
// room and water packers in between are not ported yet).
void RegisterTexturePackers(Extractor& extractor) {
    extractor.RegisterStage(MakeStage("Textures", ArchiveId::Textures, PackTextures));
    extractor.RegisterStage(MakeStage("Rareware logo", ArchiveId::Geometry, PackRarewareLogo));
    extractor.RegisterStage(MakeStage("Gun barrel", ArchiveId::Geometry, PackEyeIntro));
    extractor.RegisterStage(MakeStage("HUD images", ArchiveId::Geometry, PackHudImages));
    extractor.RegisterStage(MakeStage("Sky images", ArchiveId::Geometry, PackSkyImages));
    extractor.RegisterStage(MakeStage("Texture hits", ArchiveId::Geometry, PackTextureHits));
}

} // namespace Frigate::Extract
