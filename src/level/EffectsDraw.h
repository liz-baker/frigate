#pragma once

#include <array>
#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>

#include "gfx/FixedMtx.h"
#include "level/BgLevel.h"
#include "level/BgRender.h"
#include "level/BgVisibility.h"
#include "level/Beams.h"
#include "level/BulletImpacts.h"
#include "level/BulletSparks.h"
#include "level/Explosions.h"
#include "level/GlassShards.h"
#include "level/PlayerCamera.h"
#include "libultraship/libultra/gbi.h"

namespace Frigate::Level {

// What the effects draw under this frame.
struct EffectsFrame {
    const PlayerCamera* camera = nullptr;
    const RenderView* view = nullptr;
};

// texSelect for the image at `offset` of the global image table: the render mode
// for `mode` (1 opaque, 2 blended, 3 edge, 4 cloud) and `zbufferMode` (0 none, 1
// surface, 2 and above decal), the combiner for an intensity or colour image,
// and the call that loads it. Writes at `dl` and returns the end of the commands.
Gfx* SelectEffectImage(Gfx* dl, int32_t offset, int mode, int zbufferMode, bool intensity);

// Draws the shots' effects: the bullet beams, sparks, shattered glass, debris,
// smoke, the scorches explosions leave and the marks bullets leave on rooms
// and props.
class EffectsDraw final : public BeamDraw {
  public:
    using RandomFn = std::function<uint32_t()>;

    EffectsDraw(const BgLevel& level, const BgVisibility& visibility, const BulletSparks& sparks,
                const GlassShards& shards, Explosions& explosions, const BulletImpacts& impacts,
                const EffectImages& images)
        : level_(level), visibility_(visibility), sparks_(sparks), shards_(shards), explosions_(explosions),
          impacts_(impacts), images_(images) {
    }

    // Starts a frame: the draws of the frame before last can go.
    void BeginFrame(const EffectsFrame& frame);

    // sub_GAME_7F061E18.
    Gfx* Beam(Gfx* dl, const BeamRecord& beam, int zbufferMode, const BeamRandomFn& random) override;
    // bullet_sparks_render_all.
    Gfx* Sparks(Gfx* dl);
    // glassRenderShards.
    Gfx* Shards(Gfx* dl);
    // explosionRenderFlyingParticles.
    Gfx* Particles(Gfx* dl);

    // explosionRenderScorchBuffer and explosionCallRenderBulletImpactOnProp(NULL):
    // the scorches on the floor and the marks on the rooms.
    Gfx* RoomMarks(Gfx* dl);
    // explosionRenderBulletImpactOnProp for a prop: `matrices` are the view-space
    // matrices of its model's nodes. `drawn` is set to whether any mark was.
    Gfx* PropMarks(Gfx* dl, int prop, const Mtx::FixedMtx* matrices, size_t matrixCount, bool withAlpha, bool* drawn);
    // explosionRenderPropSmoke.
    Gfx* SmokeProp(Gfx* dl, int prop, const Prop& record, bool withAlpha);
    // explosionRenderPropExplosion: the flares of an explosion, a quad facing the
    // camera for each part, drawn with the smoke and fire image of its frame.
    Gfx* ExplosionProp(Gfx* dl, int prop, const Prop& record, bool withAlpha);

  private:
    struct Store {
        std::deque<std::vector<Vtx_t>> vertices;
        std::deque<Mtx::FixedMtx> matrices;
    };

    Vtx_t* Allocate(size_t count);
    const Mtx::FixedMtx* Keep(const float (&matrix)[4][4]);
    // applyRoomMatrixToDisplayList.
    Gfx* ApplyRoomMatrix(Gfx* dl, int room);
    // texSelect for one of the global image table's effect images.
    Gfx* SelectImage(Gfx* dl, int32_t offset, int mode, int zbufferMode) const;
    Gfx* Quad(Gfx* dl, const std::array<EffectVertex, 4>& vertices);

    const BgLevel& level_;
    const BgVisibility& visibility_;
    const BulletSparks& sparks_;
    const GlassShards& shards_;
    Explosions& explosions_;
    const BulletImpacts& impacts_;
    const EffectImages& images_;

    EffectsFrame frame_;
    std::array<Store, 2> stores_;
    size_t generation_ = 0;
    // The room matrices of this frame, by room.
    std::unordered_map<int, const Mtx::FixedMtx*> roomMatrices_;
    const Mtx::FixedMtx* smokeScale_ = nullptr;
};

} // namespace Frigate::Level
