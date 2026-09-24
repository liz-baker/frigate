#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "model/ModelWalk.h"

namespace Frigate {

// A vertex of the quad a gunfire or shadow node draws: a position in the
// node's own space, texture coordinates in 10.5 fixed point, and a colour.
struct EffectVertex {
    int16_t position[3] = { 0, 0, 0 };
    int16_t s = 0;
    int16_t t = 0;
    uint8_t colour[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
};

using EffectQuad = std::array<EffectVertex, 4>;

// The size of a textured effect's image, or none.
struct EffectImage {
    int width = 0;
    int height = 0;
};

// dogfnegx(): the muzzle flash's quad, `size` big at `offset`, turned to face
// the camera and rotated and scaled at random. `mtx` is the node's matrix,
// `modelScale` its model's scale, `random` the source of the two random
// numbers a textured flash takes and the one an untextured takes (the game's
// generator if empty). Vertices are truncated to whole units, as the game
// stores them.
EffectQuad BuildGunfireQuad(const Mat4& mtx, const float offset[3], const float size[3], float modelScale,
                            const EffectImage* image, const std::function<uint32_t()>& random = {});

// doshadow(): the shadow's quad, on the ground `height` below the root, `size`
// big about `position` (x and z in the node's space), fading with the height
// it is cast from and gone above 300 units. `alpha` is the vertices' alpha
// (see ShadowAlpha).
EffectQuad BuildShadowQuad(const float position[2], const float size[2], float height, float modelScale,
                           bool zbufferEnabled, uint8_t alpha, const EffectImage* image);

// The alpha doshadow gives its vertices: `shadowValue` (the game's global
// shadow level, sub_GAME_7F073FC8) for most props, scaled by the low byte of
// the environment colour for a character or smoke.
uint8_t ShadowAlpha(bool scaledByEnvironment, uint32_t envColour, int32_t shadowValue);

} // namespace Frigate
