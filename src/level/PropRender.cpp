#include "level/PropRender.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>

#include "gfx/GbiF3dex.h"
#include "level/DoorGeometry.h"
#include "level/GlassOpacity.h"
#include "level/Shading.h"
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

// The stored origin of a model's root group node: an object's first matrix is
// its own transform (sub_GAME_7F04AC20), so the origin never applies, and
// placement (MoveToPad) assumes the model is centred on the matrix origin.
const std::array<float, 3> kNoRootOrigin = { 0.0f, 0.0f, 0.0f };

// CHROBJ_TIMETOREGEN: the ticks over which a respawning object fades back in.
constexpr int kTimeToRegen = 60;
constexpr float kTimeToRegenF = 60.0f;

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

bool PropRender::PosIsOnScreen(const Prop& prop, float radius, const BgVisibility& visibility, const PropCamera& camera,
                               bool applyFogCull) const {
    // The first of the prop's rooms that's rendered gates the rest of the test
    // (getROOMID_isRendered).
    if (FirstRenderedRoom(prop, visibility) < 0) {
        return false;
    }
    if (!fog_.PositionIsVisibleThroughFog(prop.position, radius, camera.viewer, *camera.worldToScreen)) {
        return false;
    }
    if (applyFogCull &&
        !NearFogVisible(fog_, prop.position, radius, camera.viewer, *camera.worldToScreen, camera.lodScaleZ)) {
        return false;
    }

    // camIsPosInScreenBox against the rooms' combined bounds when they have
    // any, else camIsPosInScreen.
    ScreenRect bounds;
    bool onScreen;
    if (CombinedRoomBounds(prop, visibility, &bounds)) {
        onScreen = PositionOnScreenBox(camera.box, prop.position, radius,
                                       { bounds.left, bounds.top, bounds.right, bounds.bottom });
    } else {
        onScreen = PositionOnScreen(camera.frustum, prop.position, radius);
    }
    if (!onScreen) {
        return false;
    }
    // The final distance cutoff.
    constexpr float kMaxDistance = 32000.0f;
    const float dx = prop.position[0] - camera.viewer[0];
    const float dy = prop.position[1] - camera.viewer[1];
    const float dz = prop.position[2] - camera.viewer[2];
    return dx * dx + dy * dy + dz * dz <= kMaxDistance * kMaxDistance;
}

void PropRender::SetChildrenOnScreen(int prop, bool parentOnScreen) {
    PropList& props = objects_.Props();
    for (int child = props[prop].child; child >= 0; child = props[child].prev) {
        const PlacedObject* object = objects_.ForProp(child);
        const bool embedded = object != nullptr && (object->runtimeFlags & ObjectRuntime::Embedded) != 0;
        const bool onScreen = parentOnScreen && embedded;
        if (onScreen) {
            props[child].flags |= PropFlag::OnScreen;
        } else {
            props[child].flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
        }
        SetChildrenOnScreen(child, onScreen);
        // TODO(https://github.com/liz-baker/frigate/issues/214): chrobjWeaponTick,
        // the fuse and proximity timers of thrown and planted weapons.
    }
}

void PropRender::UpdateOnScreenList(const BgVisibility& visibility, const PropCamera& camera, const Vec3& modelPos,
                                    const Gu::MatrixF& modelviewF) {
    generation_ ^= 1;
    draws_[generation_].clear();
    chrDraw_.BeginFrame();
    PropList& props = objects_.Props();
    onScreen_.clear();

    // The transform above a character's root: the world at the origin props
    // draw relative to, then the view.
    Mtxf origin = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } };
    for (int axis = 0; axis < 3; axis++) {
        origin.m[3][axis] = -modelPos[axis];
    }
    const Mat4 chrBase = CombineWithView(origin, modelviewF);

    const Doors::Timing doorTiming{ chrs_.Timing().clockTimer, chrs_.Timing().globalTimer };
    const auto tickProp = [&](int prop) {
        Prop& record = props[prop];
        // objTick's door branch.
        if (record.type == PropType::Door && record.object >= 0) {
            doors_.Tick(record.object, doorTiming);
            if (effects_.playerPosition) {
                PlacedDoor* window = doors_.ForProp(prop);
                const PropModel* windowModel = doors_.Model(window->model);
                const bool broken = windowModel != nullptr && windowModel->skeleton == "door" &&
                                    std::find(window->hiddenSwitches.begin(), window->hiddenSwitches.end(), 1) !=
                                        window->hiddenSwitches.end();
                doors_.TickWindow(window, effects_.playerPosition(), broken);
            }
        }
        if (record.type == PropType::Explosion || record.type == PropType::Smoke) {
            if (effects_.tick) {
                effects_.tick(prop);
            }
            if ((record.flags & (PropFlag::Enabled | PropFlag::OnScreen)) == (PropFlag::Enabled | PropFlag::OnScreen)) {
                onScreen_.push_back(prop);
            }
            return;
        }
        if (record.type == PropType::Chr && record.object >= 0) {
            const ChrDraw::Advanced advanced = chrDraw_.Advance(prop, [&](const Prop& chrProp, float radius) {
                return PosIsOnScreen(chrProp, radius, visibility, camera);
            });
            if (advanced.freed) {
                return;
            }
            chrDraw_.Pose(prop, advanced.visible, chrBase, camera.lodScaleZ);
            if ((record.flags & (PropFlag::Enabled | PropFlag::OnScreen)) == (PropFlag::Enabled | PropFlag::OnScreen)) {
                onScreen_.push_back(prop);
            }
            return;
        }
        if (record.type == PropType::Obj && record.object >= 0) {
            if (const PlacedObject* removed = objects_.ForProp(prop);
                removed != nullptr && (removed->runtimeFlags & ObjectRuntime::Remove) != 0) {
                // objTick frees an object that was removed.
                if (effects_.freeObject) {
                    effects_.freeObject(prop);
                }
                objects_.Free(prop);
                return;
            }
        }
        if (objectTick_ && record.type == PropType::Obj) {
            if (PlacedObject* vehicle = objects_.ForProp(prop);
                vehicle != nullptr && (vehicle->type == static_cast<uint8_t>(PropDefType::Vehicle) ||
                                       vehicle->type == static_cast<uint8_t>(PropDefType::Aircraft))) {
                objectTick_(*vehicle);
            }
        }
        if ((record.flags & PropFlag::Enabled) == 0) {
            record.flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
            SetChildrenOnScreen(prop, false);
            return;
        }
        // Tinted glass is exempt from the near fog's cull.
        const PlacedObject* placed = record.type == PropType::Obj ? objects_.ForProp(prop) : nullptr;
        const bool tintedGlass = placed != nullptr && placed->type == static_cast<uint8_t>(PropDefType::TintedGlass);
        if (tintedGlass && effects_.playerPosition) {
            // The glass is more transparent the closer the player is, and a window
            // that is see-through has its portal closed.
            PlacedObject* glass = objects_.ForProp(prop);
            glass->calculatedOpacity = GlassOpacity(glass->runtimePosition, effects_.playerPosition(),
                                                    glass->tintDistance, glass->cullDistance, glass->tintNearOpacity);
            if (glass->portal >= 0 && effects_.setPortal) {
                effects_.setPortal(glass->portal, glass->calculatedOpacity != 0xFF);
            }
        }
        if (!PosIsOnScreen(record, ObjectRadius(record), visibility, camera, !tintedGlass)) {
            record.flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
            SetChildrenOnScreen(prop, false);
            return;
        }
        // update_color_shading: an object on screen eases its shading towards
        // its tile's.
        if (PlacedObject* object = objects_.ForProp(prop); object != nullptr && record.type == PropType::Obj) {
            EaseShading(object->shadeColour, object->nextColour);
        } else if (PlacedDoor* door = doors_.ForProp(prop); door != nullptr) {
            EaseShading(door->shadeColour, door->nextColour);
        }
        record.flags |= PropFlag::OnScreen;
        SetChildrenOnScreen(prop, true);
        // -render_pos.m[3][2]: the prop's position under the same view its
        // model draws under, measured along the camera's own axis rather than
        // straight-line distance.
        const float relX = record.position[0] - modelPos[0];
        const float relY = record.position[1] - modelPos[1];
        const float relZ = record.position[2] - modelPos[2];
        record.zDepth =
            -(relX * modelviewF[0][2] + relY * modelviewF[1][2] + relZ * modelviewF[2][2] + modelviewF[3][2]);
        onScreen_.push_back(prop);
    };
    // propsTick walks from the newest prop to the oldest, and carries on from the
    // prop before the one it ticked, so a prop added at the start of the list
    // during the walk is reached before it ends.
    for (int prop = props.ActiveTail(); prop >= 0;) {
        const int before = props.ActiveBefore(prop);
        tickProp(prop);
        prop = props.IsActive(prop) ? props.ActiveBefore(prop) : before;
    }
    // Largest zDepth first, as chraiUpdateOnscreenPropCount's selection sort
    // leaves g_OnScreenPropList.
    std::sort(onScreen_.begin(), onScreen_.end(), [&props](int a, int b) { return props[a].zDepth > props[b].zDepth; });
}

Gfx* PropRender::RenderPass(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                            const BgVisibility& visibility, int roomid, int pass, LevelId stage, const Vec3& modelPos,
                            const Gu::MatrixF& modelviewF, const PropFrame& frame) {
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
                dl = RenderProp(dl, resourceManager, *it, /*withAlpha=*/false, modelPos, modelviewF, frame);
            }
        }
    } else {
        for (int prop : onScreen_) {
            const Prop& record = props[prop];
            if (FirstRenderedRoom(record, visibility) != roomid) {
                continue;
            }
            if ((record.flags & PropFlag::Unnamed0x20) != 0) {
                dl = RenderProp(dl, resourceManager, prop, /*withAlpha=*/false, modelPos, modelviewF, frame);
            }
            dl = RenderProp(dl, resourceManager, prop, /*withAlpha=*/true, modelPos, modelviewF, frame);
        }
    }
    return dl;
}

Gfx* PropRender::RenderProp(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, int prop,
                            bool withAlpha, const Vec3& modelPos, const Gu::MatrixF& modelviewF,
                            const PropFrame& frame) {
    const Prop& record = objects_.Props()[prop];
    if (record.type == PropType::Door && record.object >= 0) {
        return RenderObject(dl, resourceManager, prop, nullptr, &doors_.All()[static_cast<size_t>(record.object)],
                            withAlpha, modelPos, modelviewF, frame);
    }
    if (record.type == PropType::Chr && record.object >= 0) {
        return chrDraw_.Render(dl, resourceManager, prop, withAlpha, frame);
    }
    if (record.type == PropType::Explosion || record.type == PropType::Smoke) {
        if (!effects_.render || !withAlpha) {
            return dl;
        }
        dl = ScissorToProp(dl, frame, record, true);
        return effects_.render(dl, prop, record, withAlpha);
    }
    if (record.type != PropType::Obj || record.object < 0) {
        // The viewer's own prop isn't drawn.
        return dl;
    }
    return RenderObject(dl, resourceManager, prop, &objects_.All()[static_cast<size_t>(record.object)], nullptr,
                        withAlpha, modelPos, modelviewF, frame);
}

Gfx* PropRender::RenderObject(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, int propId,
                              const PlacedObject* placed, const PlacedDoor* door, bool withAlpha, const Vec3& modelPos,
                              const Gu::MatrixF& modelviewF, const PropFrame& frame) {
    const Prop& record = objects_.Props()[propId];
    const PropModel* model = placed != nullptr ? objects_.Model(placed->model) : doors_.Model(door->model);
    if (model == nullptr) {
        return dl;
    }
    const uint32_t flags2 = placed != nullptr ? placed->flags2 : door->flags2;
    const ShadeColour& shade = placed != nullptr ? placed->shadeColour : door->shadeColour;
    const bool tintedGlass = placed != nullptr && placed->type == static_cast<uint8_t>(PropDefType::TintedGlass);

    FogColour fogColour{};
    const int fogResult = fog_.PropDistColour(record.zDepth, &fogColour);
    if (fogResult == Fog::kPropHidden) {
        return dl;
    }

    int objAlpha = 0xFF;
    if (!tintedGlass) {
        float visibility = FogVisibility(fog_, record.zDepth, ObjectRadius(record), frame.lodScaleZ);
        if (record.timeToRegen > 0 && record.timeToRegen < kTimeToRegen) {
            visibility *= (kTimeToRegenF - static_cast<float>(record.timeToRegen)) / kTimeToRegenF;
        }
        objAlpha = static_cast<int>(visibility * 255.0f);
        if (objAlpha <= 0) {
            return dl;
        }
    }

    // The opaque sub-draw (1) and the blended one (2) of an object drawn in
    // the usual place, or both (3) of one that always shows in front and is
    // blended whatever its alpha.
    const bool alwaysVisible = (flags2 & ObjectFlag2::AlwaysVisible) != 0;
    uint32_t passes;
    if (objAlpha < 0xFF || alwaysVisible) {
        if (!withAlpha) {
            return dl;
        }
        passes = 3;
    } else {
        passes = withAlpha ? 2 : 1;
    }

    dl = ScissorToProp(dl, frame, record, (flags2 & ObjectFlag2::Unnamed0x4000000) == 0);

    ModelRenderParams render;
    render.passes = passes;
    render.zbufferEnabled = !alwaysVisible;
    // subdraw draws the object node by node.
    render.sortJoints = false;
    if (objAlpha < 0xFF) {
        render.propType = kRenderPropWeapon;
        render.envColour = static_cast<uint32_t>(objAlpha);
    } else {
        render.propType = kRenderPropSmoke;
        // The opacity of tinted glass goes in the second byte.
        const bool windowedDoor = door != nullptr && (door->doorFlags & DoorFlag::Windowed) != 0;
        render.envColour = tintedGlass    ? static_cast<uint32_t>(placed->calculatedOpacity) << 8
                           : windowedDoor ? static_cast<uint32_t>(door->calculatedOpacity) << 8
                                          : 0;
    }
    // The shots an object has taken darken and fade its shading.
    const int shotsTaken = placed != nullptr ? Objects::ShotsTaken(*placed) : 0;
    const int shotShade = std::max(0, 0xFF - (shotsTaken * 0x15));
    IntColour colour = { (shade[0] * shotShade) >> 8, (shade[1] * shotShade) >> 8, (shade[2] * shotShade) >> 8,
                         std::min(0xFF, shade[3] + (shotsTaken * 0xF)) };
    LerpTowardsFog(colour, fogResult, fogColour);
    render.fogColour = PackColour(colour);

    // TODO(https://github.com/liz-baker/frigate/issues/214): a destroyed object's blackened
    // model (objDeform) draws with no culling and, if blended, a shade that
    // deepens with its destroyed level (objGetDestroyedLevel).
    if (placed != nullptr) {
        render.cullMode = kCullBack;
        Mtxf relative = placed->mtx;
        for (int axis = 0; axis < 3; axis++) {
            relative.m[3][axis] = placed->runtimePosition[axis] - modelPos[axis];
        }
        return DrawObjectModel(dl, resourceManager, *placed, CombineWithView(relative, modelviewF), render, withAlpha);
    }

    // sub_GAME_7F04AC20's door branch: a flipped door (opening the other way
    // from how it was placed) draws front-face culled instead of back-face,
    // and neither leaks into whatever draws next.
    render.cullMode = (door->doorFlags & DoorFlag::Flip) != 0 ? kCullFront : kCullBack;
    Mtxf relative = doors_.Transform(*door);
    for (int axis = 0; axis < 3; axis++) {
        relative.m[3][axis] -= modelPos[axis];
    }
    const std::vector<Gfx> prefix = { F3dex::ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH) };
    const std::string path = "models/prop/P" + model->name + "Z";
    const Mat4 base = CombineWithView(relative, modelviewF);
    Model::ModelDrawOverrides overrides;
    overrides.hiddenSwitchSlots = door->hiddenSwitches;
    FillDoorOverrides(resourceManager, path, *model, *door, base, &overrides);
    std::optional<Model::ModelDraw> draw =
        Model::BuildModelDraw(resourceManager, path, base, /*textures=*/true, prefix, kNoRootOrigin,
                              /*hideSwitches=*/false, &render, &overrides);
    if (!draw) {
        return dl;
    }
    dl = Emit(dl, std::move(*draw));
    const std::vector<Mtx::FixedMtx>& matrices = draws_[generation_].back().matrices;
    *dl++ = F3dex::ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH);
    if (effects_.marks) {
        PlacedDoor* mutableDoor = doors_.ForProp(propId);
        const uint8_t bit = static_cast<uint8_t>(1u << (withAlpha ? 1 : 0));
        if (mutableDoor != nullptr && (mutableDoor->marks & bit) != 0) {
            bool drawn = false;
            dl = effects_.marks(dl, propId, matrices.data(), matrices.size(), withAlpha, &drawn);
            if (!drawn) {
                mutableDoor->marks &= static_cast<uint8_t>(~bit);
            }
        }
    }
    return dl;
}

Gfx* PropRender::DrawObjectModel(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                 const PlacedObject& placed, const Mat4& base, const ModelRenderParams& render,
                                 bool withAlpha) {
    const PropModel* model = objects_.Model(placed.model);
    if (model == nullptr) {
        return dl;
    }
    const std::string path = "models/prop/P" + model->name + "Z";
    // A monitor's screens are the models' switch nodes; the game draws them
    // from a per-frame display list instead of the model's own.
    // TODO(https://github.com/liz-baker/frigate/issues/215): that list
    // (process_monitor_animation_microcode); the screens stay blank until then.
    const bool isMonitor = placed.type == static_cast<uint8_t>(PropDefType::Monitor) ||
                           placed.type == static_cast<uint8_t>(PropDefType::MultiMonitor);
    Model::ModelDrawOverrides overrides;
    overrides.hiddenSwitchSlots = placed.hiddenSwitches;
    std::optional<Model::ModelDraw> built = Model::BuildModelDraw(
        resourceManager, path, base, /*textures=*/true, /*prefix=*/{}, kNoRootOrigin, isMonitor, &render, &overrides);
    if (!built) {
        return dl;
    }
    Model::ModelDraw& draw = draws_[generation_].emplace_back(std::move(*built));
    for (const Gfx& command : draw.commands) {
        *dl++ = command;
    }

    // The marks bullets left on the object.
    if (effects_.marks) {
        PlacedObject* mutableObject = objects_.ForProp(placed.prop);
        const uint8_t bit = static_cast<uint8_t>(1u << (withAlpha ? 1 : 0));
        if (mutableObject != nullptr && (mutableObject->state & bit) != 0) {
            bool drawn = false;
            dl = effects_.marks(dl, placed.prop, draw.matrices.data(), draw.matrices.size(), withAlpha, &drawn);
            if (!drawn) {
                mutableObject->state &= static_cast<uint8_t>(~bit);
            }
        }
    }

    // The children on screen that sit on a node of this model
    // (sub_GAME_7F0442DC): their matrix is the node's, with their own
    // placement (embedment) applied first.
    PropList& props = objects_.Props();
    for (int child = props[placed.prop].child; child >= 0; child = props[child].prev) {
        const PlacedObject* embedded = objects_.ForProp(child);
        if ((props[child].flags & PropFlag::OnScreen) == 0 || embedded == nullptr ||
            (embedded->runtimeFlags & ObjectRuntime::Embedded) == 0) {
            continue;
        }
        const Mat4* node = draw.SwitchMatrix(embedded->embedSlot);
        if (node == nullptr) {
            continue;
        }
        Mtxf nodeMatrix{};
        std::memcpy(nodeMatrix.m, node->m, sizeof(nodeMatrix.m));
        const Mtxf childMatrix = Multiply(nodeMatrix, embedded->embedment);
        Mat4 childBase{};
        std::memcpy(childBase.m, childMatrix.m, sizeof(childBase.m));
        dl = DrawObjectModel(dl, resourceManager, *embedded, childBase, render, withAlpha);
    }
    return dl;
}

Mat4 PropRender::CombineWithView(const Mtxf& relative, const Gu::MatrixF& modelviewF) {
    Mtxf view{};
    std::memcpy(view.m, modelviewF, sizeof(view.m));
    const Mtxf combined = Multiply(view, relative);
    Mat4 base{};
    std::memcpy(base.m, combined.m, sizeof(base.m));
    return base;
}

Gfx* PropRender::Emit(Gfx* dl, Model::ModelDraw&& draw) {
    Model::ModelDraw& kept = draws_[generation_].emplace_back(std::move(draw));
    for (const Gfx& command : kept.commands) {
        *dl++ = command;
    }
    return dl;
}

} // namespace Frigate::Level
