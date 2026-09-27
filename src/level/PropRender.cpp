#include "level/PropRender.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "model/ModelDraw.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Level {

namespace {

// chraiGetPropRoomIds followed by the loop that finds the first of a prop's
// rooms that is currently rendered: -1 if none of them are.
int FirstRenderedRoom(const Prop& prop, const BgVisibility& visibility) {
    for (uint8_t room : prop.rooms) {
        if (room == kNoRoom) {
            break;
        }
        if (visibility.RoomRendered(room)) {
            return room;
        }
    }
    return -1;
}

} // namespace

void PropRender::UpdateOnScreenList(const BgVisibility& visibility, const Vec3& viewerPosition) {
    PropList& props = objects_.Props();
    onScreen_.clear();
    for (int prop : props.Active()) {
        Prop& record = props[prop];
        if ((record.flags & PropFlag::Enabled) == 0) {
            record.flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
            continue;
        }
        // Stand-in for the per-object tick's screen-space test (see the class
        // comment): a room-only check, not a true frustum/scissor test.
        bool onScreen = false;
        for (uint8_t room : record.rooms) {
            if (room == kNoRoom) {
                break;
            }
            if (visibility.RoomRendered(room)) {
                onScreen = true;
                break;
            }
        }
        if (!onScreen) {
            record.flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
            continue;
        }
        record.flags |= PropFlag::OnScreen;
        const float dx = record.position[0] - viewerPosition[0];
        const float dy = record.position[1] - viewerPosition[1];
        const float dz = record.position[2] - viewerPosition[2];
        record.zDepth = std::sqrt(dx * dx + dy * dy + dz * dz);
        onScreen_.push_back(prop);
    }
    // Largest zDepth first, as chraiUpdateOnscreenPropCount's selection sort
    // leaves g_OnScreenPropList.
    std::sort(onScreen_.begin(), onScreen_.end(), [&props](int a, int b) { return props[a].zDepth > props[b].zDepth; });
}

Gfx* PropRender::RenderPass(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                            const BgVisibility& visibility, int roomid, int pass, LevelId stage, const Vec3& modelPos,
                            const Gu::MatrixF& modelviewF) {
    if (stage == LevelId::Cuba) {
        if (pass == 0) {
            return dl;
        }
        if (pass == 2) {
            pass = 0;
        }
    }

    const PropList& props = objects_.Props();

    if (pass == 0 || pass == 2) {
        for (auto it = onScreen_.rbegin(); it != onScreen_.rend(); ++it) {
            const Prop& prop = props[*it];
            bool selected = false;
            const uint8_t bits = prop.flags & (PropFlag::Unnamed0x20 | PropFlag::RenderPostBg);
            if (pass == 0 && bits == 0) {
                selected = true;
            } else if (pass == 2 && bits == PropFlag::RenderPostBg) {
                selected = true;
            }
            if (selected && FirstRenderedRoom(prop, visibility) == roomid) {
                dl = RenderProp(dl, resourceManager, *it, /*withAlpha=*/false, modelPos, modelviewF);
            }
        }
    } else {
        for (int prop : onScreen_) {
            const Prop& record = props[prop];
            if (FirstRenderedRoom(record, visibility) != roomid) {
                continue;
            }
            if ((record.flags & PropFlag::Unnamed0x20) != 0) {
                dl = RenderProp(dl, resourceManager, prop, /*withAlpha=*/false, modelPos, modelviewF);
            }
            dl = RenderProp(dl, resourceManager, prop, /*withAlpha=*/true, modelPos, modelviewF);
        }
    }
    return dl;
}

Gfx* PropRender::RenderProp(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, int prop,
                            bool withAlpha, const Vec3& modelPos, const Gu::MatrixF& modelviewF) {
    // `withAlpha` selects chrobjRenderProp's opaque/blended sub-draw in the
    // decomp (fog alpha, cull mode, envcolour); this port draws the same
    // geometry either way until https://github.com/liz-baker/frigate/issues/176
    // ports that, so the pass-1 double call for PropFlag::Unnamed0x20 props
    // draws them twice.
    (void)withAlpha;
    const Prop& record = objects_.Props()[prop];
    if (record.type == PropType::Door && record.object >= 0) {
        return RenderDoor(dl, resourceManager, doors_.All()[static_cast<size_t>(record.object)], modelPos, modelviewF);
    }
    if (record.type != PropType::Obj || record.object < 0) {
        // Guards (#164), weapon pickups, explosions/smoke and the viewer's own
        // prop aren't placed as props yet.
        return dl;
    }
    return RenderObject(dl, resourceManager, objects_.All()[static_cast<size_t>(record.object)], modelPos, modelviewF);
}

Gfx* PropRender::RenderObject(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                              const PlacedObject& placed, const Vec3& modelPos, const Gu::MatrixF& modelviewF) {
    const PropModel* model = objects_.Model(placed.model);
    if (model == nullptr) {
        return dl;
    }

    // The object's own matrix is world-space (relative to model space
    // units), like a room's position; shift it into the current model
    // origin the way RoomTransform does before combining with the view.
    Mtxf relative = placed.mtx;
    for (int axis = 0; axis < 3; axis++) {
        relative.m[3][axis] -= modelPos[axis];
    }

    Mtxf view{};
    std::memcpy(view.m, modelviewF, sizeof(view.m));
    const Mtxf combined = Multiply(view, relative);

    Mat4 base{};
    std::memcpy(base.m, combined.m, sizeof(base.m));

    const std::string path = "models/prop/P" + model->name + "Z";
    std::optional<Model::ModelDraw> draw =
        Model::BuildModelDraw(resourceManager, path, base, /*textures=*/true, /*prefix=*/{});
    if (!draw) {
        return dl;
    }
    for (const Gfx& command : draw->commands) {
        *dl++ = command;
    }
    return dl;
}

Gfx* PropRender::RenderDoor(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                            const PlacedDoor& door, const Vec3& modelPos, const Gu::MatrixF& modelviewF) {
    const PropModel* model = doors_.Model(door.model);
    if (model == nullptr) {
        return dl;
    }

    Mtxf relative = doors_.Transform(door);
    for (int axis = 0; axis < 3; axis++) {
        relative.m[3][axis] -= modelPos[axis];
    }

    Mtxf view{};
    std::memcpy(view.m, modelviewF, sizeof(view.m));
    const Mtxf combined = Multiply(view, relative);

    Mat4 base{};
    std::memcpy(base.m, combined.m, sizeof(base.m));

    const std::string path = "models/prop/P" + model->name + "Z";
    std::optional<Model::ModelDraw> draw =
        Model::BuildModelDraw(resourceManager, path, base, /*textures=*/true, /*prefix=*/{});
    if (!draw) {
        return dl;
    }
    for (const Gfx& command : draw->commands) {
        *dl++ = command;
    }
    return dl;
}

} // namespace Frigate::Level
