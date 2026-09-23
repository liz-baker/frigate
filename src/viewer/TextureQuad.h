// Temporary viewer glue, see GbiF3dex.h.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "gfx/GbiF3dex.h"

namespace Ship {
class ResourceManager;
}

namespace Frigate::Viewer {

// Commands that draw one texture 1:1 with point sampling, loaded through the
// same G_SETTEX expansion a model list goes through. Empty if the texture
// can't be loaded or expanded.
std::vector<Gfx> BuildTextureQuad(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                  const std::string& texturePath, const std::vector<Gfx>& prefix);

} // namespace Frigate::Viewer
