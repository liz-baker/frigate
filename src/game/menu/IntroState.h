#pragma once

#include <cstdint>

#include "libultraship/libultra/gbi.h"

namespace Frigate::Game {

// The title.c globals shared by the Rareware logo and the gun barrel
// screens, at their cold-boot values.
struct IntroState {
    // gunbarrel_mode: the Rareware screen runs modes 0 and 1, the gun barrel
    // screen 2 through 9. Set to 2 by the gun barrel's init, and to 9 when
    // its final blackout ends.
    uint8_t gunbarrelMode = 3;
    // g_TitleX: the Rareware logo camera's distance, and the gun barrel's
    // sweep position.
    float titleX = 0.0f;
    int introEyeCounter = 0;

    // D_8002A89C: the Rareware logo's spin angle in degrees.
    float rareLogoAngle = 0.0f;

    // gunbarrelLights, DMA'd from by address (gSPLight), so it needs stable
    // storage; the Rareware screen rewrites its ambient colour every frame.
    Lights1 gunbarrelLights = gdSPDefLights1(0xDC, 0xDC, 0xDC, 0xFF, 0xFF, 0xFF, 0x00, 0x7F, 0x00);
};

} // namespace Frigate::Game
