#pragma once

#include <array>
#include <cstdint>

#include "level/BgView.h"
#include "level/BgVisibility.h"
#include "level/Fog.h"
#include "level/PropList.h"
#include "libultraship/libultra/gbi.h"

namespace Frigate::Level {

// What a prop's draw reads off the frame.
struct PropFrame {
    // bgGet2dBboxByRoomId's source.
    const BgVisibility* visibility = nullptr;
    // The player's view, which scissors are clipped to.
    PlayerView player;
    // getPlayer_c_lodscalez.
    float lodScaleZ = 1.0f;
};

// gDPSetScissor for `scissor`.
Gfx* SetScissorCommand(Gfx* dl, const Scissor& scissor);

// chrobjFogVisRangeRelated: how visible a prop `zDepth` away and `size`
// across is through the stage's near fog: 1 up to the obscuring range, then
// falling to 0 at the visible range.
float FogVisibility(const Fog& fog, float zDepth, float size, float lodScaleZ);

// sub_GAME_7F054C58: false once a prop `radius` across at `position` is far
// enough into the near fog that it is not visible, as seen from `viewer`.
bool NearFogVisible(const Fog& fog, const Vec3& position, float radius, const Vec3& viewer,
                    const Gu::MatrixF& worldToScreen, float lodScaleZ);

// getPropCombinedRoomsBBox2D: the screen area of the rooms the prop is in that
// are drawn. False if none is.
bool CombinedRoomBounds(const Prop& prop, const BgVisibility& visibility, ScreenRect* out);

// The scissor a prop draws under: the combined bounds of its rooms when it has
// them and `useRoomBounds`, else the whole view.
Gfx* ScissorToProp(Gfx* dl, const PropFrame& frame, const Prop& prop, bool useRoomBounds);

// An rgba_s32.
using IntColour = std::array<int32_t, 4>;

// lerp_rgba_s32_with_rgba_f32: when the prop is fogged (`fogResult` is
// Fog::kPropFogged), `colour` moves towards the fog colour by its alpha.
void LerpTowardsFog(IntColour& colour, int fogResult, const FogColour& fog);

// A colour as the render state's colour word (red in the top byte).
uint32_t PackColour(const IntColour& colour);

} // namespace Frigate::Level
