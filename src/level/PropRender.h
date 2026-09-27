#pragma once

#include <memory>
#include <vector>

#include "gfx/Gu.h"
#include "level/BgVisibility.h"
#include "level/LevelInfo.h"
#include "level/Matrix.h"
#include "level/Objects.h"
#include "libultraship/libultra/gbi.h"

namespace Ship {
class ResourceManager;
} // namespace Ship

namespace Frigate::Level {

// chrpropsRenderPass and the on-screen prop list it draws from (chrprop.c):
// draws the props of one room, at one of the room draw loop's three points
// (see BgRender::RenderRooms).
//
// The decomp builds the on-screen list from PROPFLAG_ONSCREEN, which the
// propobj.c per-object tick maintains from a real screen-space test; that
// tick isn't ported (https://github.com/liz-baker/frigate/issues/176), so
// UpdateOnScreenList stands in with a room-membership test (getROOMID_isRendered,
// BgVisibility::RoomRendered) against the rooms drawn this frame.
class PropRender {
  public:
    explicit PropRender(Objects& objects) : objects_(objects) {
    }

    // chraiUpdateOnscreenPropCount: this frame's candidate props, sorted by
    // distance from `viewerPosition` (see the class comment for how
    // PropFlag::OnScreen is stood in for here).
    void UpdateOnScreenList(const BgVisibility& visibility, const Vec3& viewerPosition);

    // chrpropsRenderPass: `pass` is 0 (before the room's primary list), 2
    // (after it) or 1 (after the room's secondary list). `stage` is
    // bossGetStageNum(): Cuba folds pass 2 into pass 0 and skips pass 0
    // itself, since its single giant room has no primary/secondary split to
    // separate them by.
    Gfx* RenderPass(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                    const BgVisibility& visibility, int roomid, int pass, LevelId stage, const Vec3& modelPos,
                    const Gu::MatrixF& modelviewF);

  private:
    // chrpropRender: dispatches on the prop's type. Only PropType::Obj is
    // implemented; the rest (doors #158, guards #164, weapon pickups,
    // explosions/smoke, the viewer's own prop) aren't placed as props yet.
    Gfx* RenderProp(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, int prop, bool withAlpha,
                    const Vec3& modelPos, const Gu::MatrixF& modelviewF);
    // chrobjRenderProp, reduced to loading the object's model and drawing it
    // at its placed transform; the fog/alpha/shading work `mrData` carries in
    // the decomp is https://github.com/liz-baker/frigate/issues/176.
    Gfx* RenderObject(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                      const PlacedObject& placed, const Vec3& modelPos, const Gu::MatrixF& modelviewF);

    Objects& objects_;
    // g_OnScreenPropList, sorted by zDepth descending (as
    // chraiUpdateOnscreenPropCount leaves it); g_LastOnScreenProp is just
    // this vector's end.
    std::vector<int> onScreen_;
};

} // namespace Frigate::Level
