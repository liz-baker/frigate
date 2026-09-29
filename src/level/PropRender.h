#pragma once

#include <array>
#include <deque>
#include <memory>
#include <vector>

#include "gfx/Gu.h"
#include "level/BgVisibility.h"
#include "level/Doors.h"
#include "level/Frustum.h"
#include "level/LevelInfo.h"
#include "level/Matrix.h"
#include "level/Objects.h"
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
// (getROOMID_isRendered, BgVisibility::RoomRendered) then a real
// frustum/distance screen-space test (camIsPosInScreen). It does not port
// posIsOnScreen's fog-visibility gates (fogPositionIsVisibleThroughFog,
// sub_GAME_7F054C58) or the combined-room-bbox scissor variant
// (getPropCombinedRoomsBBox2D/camIsPosInScreenBox) -- both need the bg fog
// state, https://github.com/liz-baker/frigate/issues/193. The culling
// radius is approximated from the object's collision bounding box rather
// than the model's real BoundingVolumeRadius (getinstsize) --
// https://github.com/liz-baker/frigate/issues/195.
class PropRender {
  public:
    PropRender(Objects& objects, Doors& doors) : objects_(objects), doors_(doors) {
    }

    // chraiUpdateOnscreenPropCount: this frame's candidate props, sorted by
    // distance from the camera along its view axis. `frustum` and
    // `viewerPosition` are the camera's own (PlayerCamera::Frustum/
    // WorldPosition); `modelPos` and `modelviewF` are what the props draw
    // under (see the class comment for how PropFlag::OnScreen is derived
    // here).
    void UpdateOnScreenList(const BgVisibility& visibility, const FrustumPlanes& frustum, const Vec3& viewerPosition,
                            const Vec3& modelPos, const Gu::MatrixF& modelviewF);

    // chrpropsRenderPass: `pass` is 0 (before the room's primary list), 2
    // (after it) or 1 (after the room's secondary list). `stage` is
    // bossGetStageNum(): Cuba folds pass 2 into pass 0 and skips pass 0
    // itself, since its single giant room has no primary/secondary split to
    // separate them by.
    Gfx* RenderPass(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                    const BgVisibility& visibility, int roomid, int pass, LevelId stage, const Vec3& modelPos,
                    const Gu::MatrixF& modelviewF);

  private:
    // chrpropRender: dispatches on the prop's type. PropType::Obj and
    // PropType::Door are implemented; guards (#164), weapon pickups,
    // explosions/smoke and the viewer's own prop aren't placed as props yet.
    Gfx* RenderProp(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, int prop, bool withAlpha,
                    const Vec3& modelPos, const Gu::MatrixF& modelviewF);
    // chrobjRenderProp, reduced to loading the object's model and drawing it
    // at its placed transform; the fog/alpha work `mrData` carries in the
    // decomp is https://github.com/liz-baker/frigate/issues/193, and its
    // stan-tile shading is https://github.com/liz-baker/frigate/issues/192.
    Gfx* RenderObject(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                      const PlacedObject& placed, const Vec3& modelPos, const Gu::MatrixF& modelviewF);
    // The door equivalent of RenderObject: the model at the door's live
    // transform (Doors::Transform), which door7F0526EC recomputes from its
    // openPosition every time it moves.
    Gfx* RenderDoor(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, const PlacedDoor& door,
                    const Vec3& modelPos, const Gu::MatrixF& modelviewF);
    // getinstsize, approximated from the object's model bounding box (see the
    // class comment) since BoundingVolumeRadius isn't packed yet. 0 for a
    // prop with no model or no box.
    float ObjectRadius(const Prop& prop) const;
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
