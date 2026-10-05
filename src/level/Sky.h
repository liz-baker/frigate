#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "gfx/FixedMtx.h"
#include "level/BgView.h"
#include "level/Fog.h"
#include "level/Matrix.h"
#include "libultraship/libultra/gbi.h"

namespace Ship {
class IResource;
class ResourceManager;
} // namespace Ship

namespace Frigate::Level {

// What skyRender reads off the current player: the view rectangle and the
// camera that looks out of it.
struct SkyView {
    // viGetViewLeft and its neighbours, which are also c_screenleft and its
    // neighbours.
    ViewMetrics view;
    // The view extended to either side on a display wider than 4:3 (the same
    // as `view` otherwise), and the player's view the extension maps back to.
    ViewMetrics wide;
    PlayerView player;
    // How much wider than `view` the wide view is.
    float Widen() const {
        return wide.viewWidth > view.viewWidth ? static_cast<float>(wide.viewWidth) / static_cast<float>(view.viewWidth)
                                               : 1.0f;
    }
    // c_halfwidth, c_halfheight, c_scalex and c_scaley.
    float halfWidth = 0.0f;
    float halfHeight = 0.0f;
    float scaleX = 0.0f;
    float scaleY = 0.0f;
    // bondviewGetCurrentPlayersPosition.
    Vec3 eye{};
    // currentPlayerGetViewToWorldMtxf, currentPlayerGetProjectionMatrixF and
    // camGetWorldToScreenMtxf.
    Mtxf viewToWorld{};
    Mtxf projection{};
    Mtxf worldToScreen{};
    // get_room_data_float1: the stage's scale.
    float levelScale = 1.0f;
    // getPlayerCount.
    int playerCount = 1;
};

// sky.c: the backdrop behind the level. A stage with clouds draws a cloud plane
// above the player and a sea plane below (fog-coloured, or water when the
// stage has it), each as a polygon clipped to the view; one without draws the
// fog colour over the whole view.
//
// The game hands the polygons to the RDP as raw triangle commands, which the
// interpreter has no rasteriser for. They are drawn here as ordinary vertex
// triangles: the vertices the game projects are handed on as they are, with the
// depth that perspective-correct texturing needs carried in each vertex's w.
class Sky {
  public:
    // skySetStageNum.
    void SetStageNum(LevelId stage) {
        stage_ = stage;
    }
    // sub_GAME_7F092E50 (the water's animation) and skyTick (the clouds'
    // scroll), for a tick of `clockTimer` clock ticks.
    void Tick(int clockTimer);

    // skyRender. `resourceManager` loads the sky and water images.
    Gfx* Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, const SkyView& view,
                const Fog& fog);

    // The part of sub_GAME_7F092E50 that rewrites the lists the rooms call after
    // the water textures (MipMap2C_Something_Setup and MipMap2C_Something2_Setup)
    // with this tick's scroll and cross-fade
    // (docs/formats/goldeneye-water-lists.md). Called before the rooms draw.
    void UpdateRoomWater(const std::shared_ptr<Ship::ResourceManager>& resourceManager) const;

    // g_SkyCloudOffset.
    float CloudOffset() const {
        return cloudOffset_;
    }

  private:
    // What a frame's draws own; their commands point at it.
    struct FrameData {
        std::vector<std::shared_ptr<Ship::IResource>> lists;
        std::deque<std::array<Vtx_t, 3>> vertices;
        std::deque<Mtx::FixedMtx> matrices;
    };

    // texSelect for the sky and water images: the image `index` as one tile.
    // False (after logging why) if its list doesn't load.
    bool SelectImage(Gfx*& dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, FrameData& frame,
                     int index) const;
    // sub_GAME_7F09343C: the two-layer water's tiles, cross-fade and combiner.
    Gfx* SetupWater(Gfx* dl) const;

    LevelId stage_ = LevelId::Max;
    // g_SkyCloudOffset.
    float cloudOffset_ = 0.0f;
    // flt_CODE_bss_80079E80 to 88: the water's texture scroll and the phase of
    // the cross-fade between its two layers.
    float waterScrollS_ = 0.0f;
    float waterScrollT_ = 0.0f;
    float waterPhase_ = 0.0f;
    // The draws of the last two frames.
    std::array<FrameData, 2> frames_;
    size_t generation_ = 0;
};

// The pieces of skyRender that need no display list, exposed for tests.
namespace SkyMath {

// A polygon corner's world position and how far into the sky it is (0 at the
// horizon, 1 at the top of the view).
struct Corner {
    Vec3 position{};
    float fraction = 0.0f;
    bool valid = false;
};

// skyGetWorldPosFromScreenPos.
Vec3 WorldPosFromScreenPos(const SkyView& view, float offsetX, float offsetY, float waterConcavity);
// skyIsScreenCornerInSky: whether the ray `corner` leaves the eye upward, and
// where it meets the cloud plane at height `cloudHeight`.
Corner CornerInSky(const Vec3& eye, const Vec3& corner, float cloudHeight);
// skyIsCornerInWater: the same for the plane at `waterHeight`, below.
Corner CornerInWater(const Vec3& eye, const Vec3& corner, float waterHeight);
// skyCalculateEdgeVertex: where the segment `base` to `ref` crosses y = 0.
Vec3 EdgeVertex(const Vec3& base, const Vec3& ref);

} // namespace SkyMath

} // namespace Frigate::Level
