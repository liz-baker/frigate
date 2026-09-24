#pragma once

#include <cstdint>
#include <vector>

#include "gfx/GbiF3dex.h"

// The render state the game sets before drawing a model node's lists: the
// decomp's modelApplyRenderModeType1-4 (chosen by the node's ModelType) and
// modelApplyCullMode. The lists carry geometry only, so the cycle type,
// combiner, render mode and fog/environment colours come from here.
namespace Frigate {

// Values of ModelRenderData::PropType. The decomp stores the prop type plus
// one, so these are that.
constexpr int32_t kRenderPropChr = 4;
constexpr int32_t kRenderPropWeapon = 5;
constexpr int32_t kRenderPropViewer = 7;
constexpr int32_t kRenderPropExplosion = 8;
constexpr int32_t kRenderPropSmoke = 9;

// ModelRenderData::cullmode. Both is the default and leaves the culling as the
// list set it.
constexpr uint32_t kCullBoth = 0;
constexpr uint32_t kCullNone = 1;
constexpr uint32_t kCullFront = 2;
constexpr uint32_t kCullBack = 3;

struct ModelRenderParams {
    bool zbufferEnabled = false;
    int32_t propType = 0;
    uint32_t envColour = 0; // RGBA, red in the top byte
    uint32_t fogColour = 0;
    uint32_t cullMode = kCullBoth;
    // The shadow level (sub_GAME_7F073FC8, a game-wide setting): the alpha of
    // a shadow's quad, and none is drawn at 0. The game starts it at 0x50.
    int32_t shadowLevel = 0x50;
};

// Appends the setup for a node of `modelType` (0-4; 0 sets nothing) to `out`.
// `primary` is whether its primary list is about to be drawn, not its
// secondary.
void ApplyRenderModeType(std::vector<Gfx>& out, uint8_t modelType, const ModelRenderParams& params, bool primary);

// Appends the geometry-mode change for `params.cullMode`, if it has one.
void ApplyCullMode(std::vector<Gfx>& out, const ModelRenderParams& params);

} // namespace Frigate
