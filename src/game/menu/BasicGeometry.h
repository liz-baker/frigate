#pragma once

#include "gfx/GbiF3dex.h"

namespace Frigate::Game::Menu {

// gSPDisplayList(dlBasicGeometry): every geometry mode off but smooth
// shading, and texturing off.
inline Gfx* BasicGeometry(Gfx* dl) {
    using namespace Frigate::F3dex;
    *dl++ =
        ClearGeometryMode(G_SHADE | Fast::F3DEX_G_SHADING_SMOOTH | Fast::F3DEX_G_CULL_FRONT | Fast::F3DEX_G_CULL_BACK |
                          G_FOG | G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_LOD);
    *dl++ = Texture(0, 0, 0, 0, 0);
    *dl++ = SetGeometryMode(G_SHADE | Fast::F3DEX_G_SHADING_SMOOTH);
    return dl;
}

} // namespace Frigate::Game::Menu
