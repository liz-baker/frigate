#pragma once

#include <array>
#include <deque>
#include <memory>
#include <vector>

#include "gfx/Gu.h"
#include "level/BgVisibility.h"
#include "level/ChrDraw.h"
#include "level/Chrs.h"
#include "level/Doors.h"
#include "level/Fog.h"
#include "level/Frustum.h"
#include "level/LevelInfo.h"
#include "level/Matrix.h"
#include "level/Objects.h"
#include "level/PropFog.h"
#include "libultraship/libultra/gbi.h"
#include "model/ModelDraw.h"

namespace Ship {
class ResourceManager;
} // namespace Ship

namespace Frigate::Level {

// chrpropsRenderPass and the on-screen prop list it draws from (chrprop.c):
// draws the props of one room, at one of the room draw loop's three points
// (see BgRender::RenderRooms).
//
// UpdateOnScreenList maintains PROPFLAG_ONSCREEN the way the propobj.c
// per-object tick's posIsOnScreen does: a room-membership gate
// (getROOMID_isRendered, BgVisibility::RoomRendered), the fog gates
// (fogPositionIsVisibleThroughFog and sub_GAME_7F054C58) and a screen-space
// test (camIsPosInScreen, or camIsPosInScreenBox against the rooms' combined
// bounds). The culling radius is approximated from the object's collision
// bounding box rather than the model's real BoundingVolumeRadius
// (getinstsize) -- https://github.com/liz-baker/frigate/issues/195. A
// character's is its model's, which the body table carries.

// The camera as the on-screen tests and the fog fade read it.
struct PropCamera {
    // camIsPosInScreen's planes and camIsPosInScreenBox's camera.
    FrustumPlanes frustum;
    BoxCamera box;
    // bondviewGetCurrentPlayersPosition.
    Vec3 viewer{};
    // camGetWorldToScreenMtxf.
    const Gu::MatrixF* worldToScreen = nullptr;
    // getPlayer_c_lodscalez.
    float lodScaleZ = 1.0f;
};

class PropRender {
  public:
    PropRender(Objects& objects, Doors& doors, Chrs& chrs, const Fog& fog)
        : objects_(objects), doors_(doors), chrs_(chrs), fog_(fog), chrDraw_(chrs, objects, fog) {
    }

    // chrlvActionTick for each character propsTick reaches.
    void SetChrActionTick(std::function<void(Chr&)> tick) {
        chrDraw_.SetActionTick(std::move(tick));
    }

    // chraiUpdateOnscreenPropCount: this frame's candidate props, sorted by
    // distance from the camera along its view axis. `camera` is the
    // player's; `modelPos` and `modelviewF` are what the props draw under
    // (see the class comment for how PropFlag::OnScreen is derived here).
    // This is also where the props tick that shows on screen: each character
    // animates and poses (chrTick), an object's shading eases towards its
    // tile's, and its children take their on-screen flag from it.
    void UpdateOnScreenList(const BgVisibility& visibility, const PropCamera& camera, const Vec3& modelPos,
                            const Gu::MatrixF& modelviewF);

    // The props on screen this frame, farthest first.
    const std::vector<int>& OnScreen() const {
        return onScreen_;
    }

    // chrpropsRenderPass: `pass` is 0 (before the room's primary list), 2
    // (after it) or 1 (after the room's secondary list). `stage` is
    // bossGetStageNum(): Cuba folds pass 2 into pass 0 and skips pass 0
    // itself, since its single giant room has no primary/secondary split to
    // separate them by.
    Gfx* RenderPass(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                    const BgVisibility& visibility, int roomid, int pass, LevelId stage, const Vec3& modelPos,
                    const Gu::MatrixF& modelviewF, const PropFrame& frame);

  private:
    // chrpropRender: dispatches on the prop's type. PropType::Obj,
    // PropType::Door and PropType::Chr are implemented; explosions/smoke and
    // the viewer's own prop aren't placed as props yet.
    Gfx* RenderProp(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, int prop, bool withAlpha,
                    const Vec3& modelPos, const Gu::MatrixF& modelviewF, const PropFrame& frame);
    // chrobjRenderProp for an object or a door: how far the fog and the
    // object's own state fade it, which of the opaque and blended sub-draws
    // it is drawn in, and the shading and fog colour it draws with.
    // `placed` and `door` are the record `prop` belongs to (one of them null).
    // The object is placed at its transform (an object's, or a door's live
    // one: Doors::Transform, which door7F0526EC recomputes from its
    // openPosition every time it moves).
    Gfx* RenderObject(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, int prop,
                      const PlacedObject* placed, const PlacedDoor* door, bool withAlpha, const Vec3& modelPos,
                      const Gu::MatrixF& modelviewF, const PropFrame& frame);
    // sub_GAME_7F04AC20's model draw for `placed` under `base`, then its
    // children that are on screen and embedded in one of its nodes, each under
    // the node's matrix.
    Gfx* DrawObjectModel(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                         const PlacedObject& placed, const Mat4& base, const ModelRenderParams& render);
    // getinstsize, approximated from the object's model bounding box (see the
    // class comment) since BoundingVolumeRadius isn't packed yet. 0 for a
    // prop with no model or no box.
    float ObjectRadius(const Prop& prop) const;
    // posIsOnScreen: `applyFogCull` is its fourth argument.
    bool PosIsOnScreen(const Prop& prop, float radius, const BgVisibility& visibility, const PropCamera& camera,
                       bool applyFogCull = true) const;
    // The on-screen flag `prop`'s children take (sub_GAME_7F0442DC and
    // sub_GAME_7F04424C): an embedded child is on screen with its parent, any
    // other is not.
    void SetChildrenOnScreen(int prop, bool parentOnScreen);
    // Appends `draw`'s commands to the display list and keeps `draw` alive:
    // its commands point at the matrices and lists it owns, and the
    // interpreter reads them after this frame's list is built.
    Gfx* Emit(Gfx* dl, Model::ModelDraw&& draw);
    // The prop's matrix under the view: `relative` (placed at the prop's world position less modelPos), then the view.
    // Neither the props nor the rooms take the stage's visibility scale (a fixed-point range device), so their depths
    // agree in the z-buffer.
    Mat4 CombineWithView(const Mtxf& relative, const Gu::MatrixF& modelviewF) const;

    Objects& objects_;
    Doors& doors_;
    Chrs& chrs_;
    const Fog& fog_;
    ChrDraw chrDraw_;
    // g_OnScreenPropList, sorted by zDepth descending (as
    // chraiUpdateOnscreenPropCount leaves it); g_LastOnScreenProp is just
    // this vector's end.
    std::vector<int> onScreen_;
    // The draws of the last two frames, so a frame still being drawn keeps its
    // matrices while the next one is built.
    std::array<std::deque<Model::ModelDraw>, 2> draws_;
    size_t generation_ = 0;
};

} // namespace Frigate::Level
