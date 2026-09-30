#include "level/Fog.h"

#include <cfloat>

#include "gfx/GbiF3dex.h"

namespace Frigate::Level {

namespace {

// The lookup by id that fogLoadLevelEnvironment's three passes over fog_tables
// share.
int FindEnvironment(int32_t id) {
    const std::span<const EnvironmentRecord> table = EnvironmentTable();
    for (size_t i = 0; i < table.size(); i++) {
        if (table[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// gSPFogPosition's word for the fog running from `min` to `max`, in thousandths
// of the z range.
uint32_t FogPositionWord(int32_t min, int32_t max) {
    const uint32_t multiplier = static_cast<uint32_t>(128000 / (max - min)) & 0xFFFF;
    const uint32_t offset = static_cast<uint32_t>((500 - min) * 256 / (max - min)) & 0xFFFF;
    return (multiplier << 16) | offset;
}

} // namespace

void Fog::SetZRange(float zNear, float zFar) {
    zNear_ = zNear;
    zFar_ = zFar;
}

void Fog::LoadCurrentEnvironment(const EnvironmentRecord& record) {
    SetZRange(record.blendMultiplier, record.farFog);
    float zrange[2] = { zNear_, zFar_ };

    const float scale = visibilityScale_;
    zrange[0] /= scale;
    zrange[1] /= scale;

    const float differenceFromFar = static_cast<float>(record.differenceFromFarIntensity) / 1000.0f;
    const float farIntensity = static_cast<float>(record.farIntensity) / 1000.0f;

    scaledFarFogIntensity_ = ((zrange[1] - zrange[0]) * farIntensity) + zrange[0];

    const float blendMultiplier = record.blendMultiplier / scale;
    const float farFog = record.farFog / scale;

    // The numerator is the constant 128.
    const float sp20 = (256.0f * (0.5f - 0.0f)) / (farIntensity - differenceFromFar);
    const float sp1C = (256.0f * (0.5f - differenceFromFar)) / (farIntensity - differenceFromFar);

    farFogDistScaled_ = ((farFog * -sp20 * (blendMultiplier + 1.0f)) / (farFog - blendMultiplier)) / 255.0f;
    nearFogDistScaled_ = ((sp20 * (farFog + 1.0f) / (farFog - blendMultiplier)) + sp1C) / 255.0f;

    current_.differenceFromFarIntensity = record.differenceFromFarIntensity;
    current_.farIntensity = record.farIntensity;
    current_.sky = record.sky;

    if (record.nfd.nearFog == 0.0f) {
        nearFogSet_ = false;
    } else {
        nearFog_ = record.nfd;
        nearFogSet_ = true;
    }

    enabled_ = true;
}

void Fog::LoadLevelEnvironment(LevelId level, bool cinema, int playerCount, float visibilityScale) {
    visibilityScale_ = visibilityScale;
    const int32_t stage = static_cast<int32_t>(level);
    const int32_t players = playerCount == 1 ? 0 : playerCount;

    scaledFarFogIntensity_ = FLT_MAX;

    if (cinema) {
        if (const int found = FindEnvironment(stage + 900); found >= 0) {
            mainRecord_ = found;
            LoadCurrentEnvironment(EnvironmentTable()[static_cast<size_t>(found)]);
            return;
        }
    }

    if (const int found = FindEnvironment(stage + (players * 100)); found >= 0) {
        mainRecord_ = found;
        LoadCurrentEnvironment(EnvironmentTable()[static_cast<size_t>(found)]);
        return;
    }

    if (players >= 2) {
        if (const int found = FindEnvironment(players * 100); found >= 0) {
            mainRecord_ = found;
            LoadCurrentEnvironment(EnvironmentTable()[static_cast<size_t>(found)]);
            return;
        }
    }

    SetZRange(15.0f, 10000.0f);
    enabled_ = false;

    // The last row for the stage, else the first row.
    const SkyEnvironment* sky = &FoglessEnvironmentTable()[0].second;
    for (const auto& row : FoglessEnvironmentTable()) {
        if (row.first == stage) {
            sky = &row.second;
        }
    }
    current_.sky = *sky;
}

void Fog::SwitchToSolosky2(float fraction) {
    if (mainRecord_ < 0) {
        return;
    }
    const std::span<const EnvironmentRecord> table = EnvironmentTable();
    const EnvironmentRecord& main = table[static_cast<size_t>(mainRecord_)];
    // g_EnvironmentAltp: the record after the main one.
    if (static_cast<size_t>(mainRecord_) + 1 >= table.size()) {
        return;
    }
    const EnvironmentRecord& alt = table[static_cast<size_t>(mainRecord_) + 1];

    EnvironmentRecord blended = main;
    blended.blendMultiplier = main.blendMultiplier + (fraction * (alt.blendMultiplier - main.blendMultiplier));
    blended.farFog = main.farFog + (fraction * (alt.farFog - main.farFog));
    blended.differenceFromFarIntensity =
        static_cast<int32_t>(static_cast<float>(main.differenceFromFarIntensity) +
                             (fraction * (static_cast<float>(alt.differenceFromFarIntensity) -
                                          static_cast<float>(main.differenceFromFarIntensity))));
    blended.farIntensity = static_cast<int32_t>(
        static_cast<float>(main.farIntensity) +
        (fraction * (static_cast<float>(alt.farIntensity) - static_cast<float>(main.farIntensity))));
    blended.sky.red = static_cast<uint8_t>(static_cast<float>(main.sky.red) +
                                           (fraction * (static_cast<float>(alt.sky.red) - main.sky.red)));
    blended.sky.green = static_cast<uint8_t>(static_cast<float>(main.sky.green) +
                                             (fraction * (static_cast<float>(alt.sky.green) - main.sky.green)));
    blended.sky.blue = static_cast<uint8_t>(static_cast<float>(main.sky.blue) +
                                            (fraction * (static_cast<float>(alt.sky.blue) - main.sky.blue)));
    blended.sky.red &= 0xf8;
    blended.sky.green &= 0xf8;
    blended.sky.blue &= 0xf8;

    LoadCurrentEnvironment(blended);
}

Gfx* Fog::SetRenderFogColor(Gfx* dl) const {
    if (!enabled_) {
        return dl;
    }
    gDPSetFogColor(dl++, current_.sky.red, current_.sky.green, current_.sky.blue, 0xff);
    // gSPFogPosition: a G_MW_FOG move-word.
    constexpr uint32_t kFogOffsetIndex = 0;
    *dl++ = F3dex::Command(Fast::F3DEX_G_MOVEWORD, (kFogOffsetIndex << 8) | G_MW_FOG,
                           FogPositionWord(current_.differenceFromFarIntensity, current_.farIntensity));
    *dl++ = F3dex::SetGeometryMode(G_FOG);
    *dl++ = F3dex::SetOtherModeH(G_MDSFT_ALPHADITHER, 2, G_AD_NOISE);
    return dl;
}

Gfx* Fog::ClearFogMode(Gfx* dl) const {
    if (!enabled_) {
        return dl;
    }
    *dl++ = F3dex::ClearGeometryMode(G_FOG);
    return dl;
}

bool Fog::PositionIsVisibleThroughFog(const Vec3& position, float range, const Vec3& viewer,
                                      const Gu::MatrixF& worldToScreen) const {
    if (!enabled_) {
        return true;
    }
    const float dx = position[0] - viewer[0];
    const float dy = position[1] - viewer[1];
    const float dz = position[2] - viewer[2];
    const float depth = (dx * worldToScreen[0][0]) + (dy * worldToScreen[0][1]) + (dz * worldToScreen[0][2]);
    return !(depth > (scaledFarFogIntensity_ + range));
}

int Fog::PropDistColour(float zDepth, FogColour* colour) const {
    if (!enabled_) {
        return kPropClear;
    }
    if (zDepth < 0.0f) {
        return kPropClear;
    }

    (*colour)[0] = static_cast<float>(current_.sky.red) / 255.0f;
    (*colour)[1] = static_cast<float>(current_.sky.green) / 255.0f;
    (*colour)[2] = static_cast<float>(current_.sky.blue) / 255.0f;
    (*colour)[3] = (farFogDistScaled_ / zDepth) + nearFogDistScaled_;

    if ((*colour)[3] < 0.0f) {
        return kPropClear;
    }
    if ((*colour)[3] > 1.0f) {
        return kPropHidden;
    }
    return kPropFogged;
}

} // namespace Frigate::Level
