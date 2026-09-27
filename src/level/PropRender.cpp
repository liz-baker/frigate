#include "level/PropRender.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>

#include "gfx/GbiF3dex.h"
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

float PropRender::ObjectRadius(const Prop& prop) const {
    std::optional<ModelBounds> bounds;
    float scale = 1.0f;
    if (prop.type == PropType::Door && prop.object >= 0) {
        const PlacedDoor& door = doors_.All()[static_cast<size_t>(prop.object)];
        bounds = door.modelBounds;
    } else if (prop.type == PropType::Obj && prop.object >= 0) {
        const PlacedObject& placed = objects_.All()[static_cast<size_t>(prop.object)];
        if (const PropModel* model = objects_.Model(placed.model)) {
            bounds = model->bounds;
        }
        scale = placed.scale;
    }
    if (!bounds) {
        return 0.0f;
    }
    const float dx = bounds->xmax - bounds->xmin;
    const float dy = bounds->ymax - bounds->ymin;
    const float dz = bounds->zmax - bounds->zmin;
    return 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz) * scale;
}

void PropRender::UpdateOnScreenList(const BgVisibility& visibility, const FrustumPlanes& frustum,
                                    const Vec3& viewerPosition, const Vec3& modelPos, const Gu::MatrixF& modelviewF) {
    PropList& props = objects_.Props();
    onScreen_.clear();
    for (int prop : props.Active()) {
        Prop& record = props[prop];
        if ((record.flags & PropFlag::Enabled) == 0) {
            record.flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
            continue;
        }
        // posIsOnScreen: the first of the prop's rooms that's rendered gates
        // the rest of the test (getROOMID_isRendered).
        if (FirstRenderedRoom(record, visibility) < 0) {
            record.flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
            continue;
        }
        // camIsPosInScreen (the fog-visibility gates ahead of it need the bg
        // fog state -- see the class comment).
        if (!PositionOnScreen(frustum, record.position, ObjectRadius(record))) {
            record.flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
            continue;
        }
        // posIsOnScreen's final distance cutoff.
        constexpr float kMaxDistance = 32000.0f;
        const float dx = record.position[0] - viewerPosition[0];
        const float dy = record.position[1] - viewerPosition[1];
        const float dz = record.position[2] - viewerPosition[2];
        if (dx * dx + dy * dy + dz * dz > kMaxDistance * kMaxDistance) {
            record.flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
            continue;
        }
        record.flags |= PropFlag::OnScreen;
        // -render_pos.m[3][2]: the prop's position under the same view its
        // model draws under, measured along the camera's own axis rather than
        // straight-line distance.
        const float relX = record.position[0] - modelPos[0];
        const float relY = record.position[1] - modelPos[1];
        const float relZ = record.position[2] - modelPos[2];
        record.zDepth =
            -(relX * modelviewF[0][2] + relY * modelviewF[1][2] + relZ * modelviewF[2][2] + modelviewF[3][2]);
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
    // geometry either way until https://github.com/liz-baker/frigate/issues/193
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

    // placed.mtx carries rotation/scale only (SetPosition never writes its
    // translation row); the object's actual world position lives in
    // runtimePosition, the same split UpdateCollision folds back together
    // before using the matrix for world-space bounds. Combine them here and
    // shift into the current model origin the way RoomTransform does before
    // combining with the view.
    Mtxf relative = placed.mtx;
    for (int axis = 0; axis < 3; axis++) {
        relative.m[3][axis] = placed.runtimePosition[axis] - modelPos[axis];
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

    // sub_GAME_7F04AC20's door branch: a flipped door (opening the other way
    // from how it was placed) draws front-face culled instead of back-face.
    const std::vector<Gfx> prefix = { F3dex::ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH),
                                      F3dex::SetGeometryMode((door.doorFlags & DoorFlag::Flip) != 0
                                                                 ? Fast::F3DEX_G_CULL_FRONT
                                                                 : Fast::F3DEX_G_CULL_BACK) };

    const std::string path = "models/prop/P" + model->name + "Z";
    std::optional<Model::ModelDraw> draw =
        Model::BuildModelDraw(resourceManager, path, base, /*textures=*/true, prefix);
    if (!draw) {
        return dl;
    }
    for (const Gfx& command : draw->commands) {
        *dl++ = command;
    }
    // sub_GAME_7F04AC20 clears the cull-mode bits again right after the
    // door's own subdraw, so the flip above doesn't leak into whatever draws
    // next this frame.
    *dl++ = F3dex::ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH);
    return dl;
}

} // namespace Frigate::Level
