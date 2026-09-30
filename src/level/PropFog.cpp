#include "level/PropFog.h"

#include <algorithm>

namespace Frigate::Level {

Gfx* SetScissorCommand(Gfx* dl, const Scissor& scissor) {
    gDPSetScissor(dl++, G_SC_NON_INTERLACE, scissor.left, scissor.top, scissor.right, scissor.bottom);
    return dl;
}

float FogVisibility(const Fog& fog, float zDepth, float size, float lodScaleZ) {
    float visibility = 1.0f;
    const NearFog* nearFog = fog.NearFogValues();

    if (nearFog != nullptr && nearFog->maxObfuscationRange < zDepth) {
        const float scaled =
            ((((zDepth - nearFog->maxObfuscationRange) * 100.0f) / size) + nearFog->maxObfuscationRange) * lodScaleZ;

        if (nearFog->maxVisRange <= scaled) {
            visibility = 0.0f;
        } else if (nearFog->nearFog < scaled) {
            visibility = (nearFog->maxVisRange - scaled) / (nearFog->maxVisRange - nearFog->nearFog);
        }
    }

    return visibility;
}

bool NearFogVisible(const Fog& fog, const Vec3& position, float radius, const Vec3& viewer,
                    const Gu::MatrixF& worldToScreen, float lodScaleZ) {
    const NearFog* nearFog = fog.NearFogValues();
    if (nearFog == nullptr) {
        return true;
    }
    const float dx = position[0] - viewer[0];
    const float dy = position[1] - viewer[1];
    const float dz = position[2] - viewer[2];
    float depth = dx * worldToScreen[0][0] + dy * worldToScreen[0][1] + dz * worldToScreen[0][2];
    if (depth > nearFog->maxObfuscationRange) {
        depth = ((depth - nearFog->maxObfuscationRange) * 100 / radius + nearFog->maxObfuscationRange) * lodScaleZ;
        if (depth >= nearFog->maxVisRange) {
            return false;
        }
    }
    return true;
}

bool CombinedRoomBounds(const Prop& prop, const BgVisibility& visibility, ScreenRect* out) {
    bool found = false;
    for (const uint8_t room : prop.rooms) {
        if (room == kNoRoom) {
            break;
        }
        ScreenRect bounds;
        if (!visibility.GetRoomBounds2d(room, bounds)) {
            continue;
        }
        if (found) {
            out->left = std::min(out->left, bounds.left);
            out->top = std::min(out->top, bounds.top);
            out->right = std::max(out->right, bounds.right);
            out->bottom = std::max(out->bottom, bounds.bottom);
        } else {
            *out = bounds;
        }
        found = true;
    }
    return found;
}

Gfx* ScissorToProp(Gfx* dl, const PropFrame& frame, const Prop& prop, bool useRoomBounds) {
    ScreenRect bounds;
    if (useRoomBounds && frame.visibility != nullptr && CombinedRoomBounds(prop, *frame.visibility, &bounds)) {
        return SetScissorCommand(dl, BoundsScissor(frame.player, bounds));
    }
    return SetScissorCommand(dl, ViewScissor(frame.player));
}

void LerpTowardsFog(IntColour& colour, int fogResult, const FogColour& fog) {
    if (fogResult != Fog::kPropFogged) {
        return;
    }
    const float r = fog[0] * 255.0f;
    const float g = fog[1] * 255.0f;
    const float b = fog[2] * 255.0f;
    const float a = fog[3];

    colour[0] = static_cast<int32_t>((a * (r - static_cast<float>(colour[0]))) + static_cast<float>(colour[0]));
    colour[1] = static_cast<int32_t>((a * (g - static_cast<float>(colour[1]))) + static_cast<float>(colour[1]));
    colour[2] = static_cast<int32_t>((a * (b - static_cast<float>(colour[2]))) + static_cast<float>(colour[2]));
    colour[3] = static_cast<int32_t>((a * (255.0f - static_cast<float>(colour[3]))) + static_cast<float>(colour[3]));
}

uint32_t PackColour(const IntColour& colour) {
    return (static_cast<uint32_t>(colour[0]) << 24) | (static_cast<uint32_t>(colour[1]) << 16) |
           (static_cast<uint32_t>(colour[2]) << 8) | static_cast<uint32_t>(colour[3]);
}

} // namespace Frigate::Level
