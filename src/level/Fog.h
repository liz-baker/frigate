#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <utility>

#include "gfx/Gu.h"
#include "level/BgHeader.h"
#include "level/LevelInfo.h"
#include "libultraship/libultra/gbi.h"

namespace Frigate::Level {

// NearFogRecord: the distances a prop fades out over (1 / 100 of a prop's size
// per unit of its distance beyond `maxObfuscationRange`).
struct NearFog {
    float nearFog = 0.0f;
    float maxVisRange = 0.0f;
    float maxObfuscationRange = 0.0f;
};

// SkyBoxRecord: the sky, cloud and water settings of an environment.
struct SkyEnvironment {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    uint8_t clouds = 0;
    // The cloud plane's height (canonically skyheight).
    float cloudRepeat = 0.0f;
    int16_t skyImageId = 0;
    float cloudRed = 0.0f;
    float cloudGreen = 0.0f;
    float cloudBlue = 0.0f;
    uint8_t isWater = 0;
    // The water plane's height (canonically seaheight).
    float waterRepeat = 0.0f;
    int16_t waterImageId = 0;
    float waterRed = 0.0f;
    float waterGreen = 0.0f;
    float waterBlue = 0.0f;
    float waterConcavity = 0.0f;
};

// EnvironmentRecord: a stage's z range, fog and sky. The record's MinVisrange
// and Intensity words are not read by anything and are not kept.
struct EnvironmentRecord {
    // The stage id plus a variant (kAlt, kCinema, or the player count times
    // 100); 0 ends the table.
    int32_t id = 0;
    // VisibilityRecord: the z range's near and far distances.
    float blendMultiplier = 0.0f;
    float farFog = 0.0f;
    NearFog nfd;
    // FogRecord: where the fog starts and ends, in thousandths of the z range.
    int32_t differenceFromFarIntensity = 0;
    int32_t farIntensity = 0;
    SkyEnvironment sky;
};

// The environment ids' variants (ENVIRONMENTDATA_IDS).
constexpr int32_t kAlt = 100;
constexpr int32_t kPlayers2 = 200;
constexpr int32_t kPlayers3 = 300;
constexpr int32_t kPlayers4 = 400;
constexpr int32_t kCinema = 900;

// fog_tables and fog_tables2. The first ends with an id of 0, the second (the
// stages that have no fog, only a sky) with the same.
std::span<const EnvironmentRecord> EnvironmentTable();
std::span<const std::pair<int32_t, SkyEnvironment>> FoglessEnvironmentTable();

// CurrentEnvironmentRecord: the environment the level is drawing with.
struct CurrentEnvironment {
    int32_t differenceFromFarIntensity = 0x384;
    int32_t farIntensity = 0x3e8;
    SkyEnvironment sky;
};

// rgba_f32 as fogGetPropDistColor fills it: the fog colour in 0 to 1 and the
// fraction of it that covers a prop.
using FogColour = std::array<float, 4>;

// bgfog.c: the stage's fog and sky environment, and the z range it sets. The
// state g_CurrentEnvironment and its neighbours hold in the game.
class Fog {
  public:
    // fogGetPropDistColor's results.
    static constexpr int kPropHidden = 0;
    static constexpr int kPropFogged = 1;
    static constexpr int kPropClear = 2;

    // fogLoadLevelEnvironment: picks `level`'s record for `playerCount`
    // players (or its cinema record when `cinema`), else a multiplayer default,
    // else the stage's fogless sky. `visibilityScale` is bgGetLevelVisibilityScale.
    void LoadLevelEnvironment(LevelId level, bool cinema, int playerCount, float visibilityScale);

    // fogSwitchToSolosky2: the environment `fraction` of the way from the
    // stage's main record to its alternate (the record after it), as the
    // toxic gas fades the level in.
    void SwitchToSolosky2(float fraction);

    // g_FogSkyIsEnabled.
    bool Enabled() const {
        return enabled_;
    }
    // fogGetCurrentEnvironmentp.
    const CurrentEnvironment& Environment() const {
        return current_;
    }
    // viGetZRange: the near and far distances the projection is built from.
    float ZNear() const {
        return zNear_;
    }
    float ZFar() const {
        return zFar_;
    }
    // fogGetScaledFarFogIntensity(Squared).
    float ScaledFarFogIntensity() const {
        return scaledFarFogIntensity_;
    }
    float ScaledFarFogIntensitySquared() const {
        return scaledFarFogIntensity_ * scaledFarFogIntensity_;
    }
    // fogGetNearFogValuesP: null when the record has no near fog.
    const NearFog* NearFogValues() const {
        return nearFogSet_ ? &nearFog_ : nullptr;
    }

    // fogSetRenderFogColor (both of its arms are the same): the fog colour and
    // position and the geometry mode that applies them, or nothing while the
    // stage has no fog.
    Gfx* SetRenderFogColor(Gfx* dl) const;
    // fogRenderClearFogMode.
    Gfx* ClearFogMode(Gfx* dl) const;

    // fogPositionIsVisibleThroughFog: false if `position` lies beyond the far
    // fog by more than `range`, measured along the view axis in `worldToScreen`
    // from `viewer`.
    bool PositionIsVisibleThroughFog(const Vec3& position, float range, const Vec3& viewer,
                                     const Gu::MatrixF& worldToScreen) const;

    // fogGetPropDistColor: kPropClear (nothing obscures the prop),
    // kPropFogged (`colour` covers it by its alpha) or kPropHidden (the fog
    // hides it), for a prop `zDepth` from the camera.
    int PropDistColour(float zDepth, FogColour* colour) const;

  private:
    // fogLoadCurrentEnvironment.
    void LoadCurrentEnvironment(const EnvironmentRecord& record);
    void SetZRange(float zNear, float zFar);

    bool enabled_ = false;
    float zNear_ = 15.0f;
    float zFar_ = 10000.0f;
    float visibilityScale_ = 1.0f;
    CurrentEnvironment current_;
    // g_ScaledFarFogIntensity. g_ScaledDifferenceFromFarFogIntensity, which
    // sits beside it, has no reader.
    float scaledFarFogIntensity_ = 3.402823466e+38f;
    // g_CurFogDetails's far_fog_dist_scaled and near_fog_dist_scaled.
    float farFogDistScaled_ = 0.0f;
    float nearFogDistScaled_ = 0.0f;
    NearFog nearFog_;
    bool nearFogSet_ = false;
    // g_EnvironmentMainp and g_EnvironmentAltp, as indexes into the table (-1
    // when the stage has no fog record).
    int mainRecord_ = -1;
};

} // namespace Frigate::Level
