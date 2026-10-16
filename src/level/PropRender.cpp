#include "level/PropRender.h"
#include "level/EffectsDraw.h"
#include "level/Rotor.h"
#include "level/VehicleWheels.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>

#include "gfx/GbiF3dex.h"
#include "level/DoorGeometry.h"
#include "level/GlassOpacity.h"
#include "level/Projectiles.h"
#include "level/Shading.h"
#include "resource/type/ModelTree.h"
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
    // getinstsize: the model's BoundingVolumeRadius at the instance's scale.
    if (prop.type == PropType::Door && prop.object >= 0) {
        const PlacedDoor& door = doors_.All()[static_cast<size_t>(prop.object)];
        const PropModel* model = doors_.Model(door.model);
        return model != nullptr ? model->radius * door.scale : 0.0f;
    }
    if ((prop.type == PropType::Obj || prop.type == PropType::Weapon) && prop.object >= 0) {
        const PlacedObject& placed = objects_.All()[static_cast<size_t>(prop.object)];
        const PropModel* model = objects_.Model(placed.model);
        return model != nullptr ? model->radius * placed.scale : 0.0f;
    }
    return 0.0f;
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
    for (int child = props[prop].child; child >= 0;) {
        const int previous = props[child].prev;
        const PlacedObject* object = objects_.ForProp(child);
        if (object != nullptr && (object->runtimeFlags & ObjectRuntime::Remove) != 0) {
            // sub_GAME_7F0442DC and sub_GAME_7F04424C free a removed child.
            if (effects_.freeObject) {
                effects_.freeObject(child);
            }
            objects_.Free(child, (object->state & ObjectState::Respawn) != 0);
            child = previous;
            continue;
        }
        const bool embedded = object != nullptr && (object->runtimeFlags & ObjectRuntime::Embedded) != 0;
        const bool onScreen = parentOnScreen && embedded;
        if (onScreen) {
            props[child].flags |= PropFlag::OnScreen;
        } else {
            props[child].flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
        }
        if (projectiles_ != nullptr && weaponTickPass_) {
            projectiles_->WeaponTick(child);
        }
        SetChildrenOnScreen(child, onScreen);
        child = previous;
    }
}

void PropRender::UpdateOnScreenList(const BgVisibility& visibility, const PropCamera& camera, const Vec3& modelPos,
                                    const Gu::MatrixF& modelviewF, bool newFrame) {
    if (newFrame) {
        generation_ ^= 1;
        draws_[generation_].clear();
    }
    chrDraw_.BeginFrame(newFrame);
    PropList& props = objects_.Props();
    onScreen_.clear();

    // The transform above a character's root: the world at the origin props
    // draw relative to, then the view.
    Mtxf origin = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } };
    for (int axis = 0; axis < 3; axis++) {
        origin.m[3][axis] = -modelPos[axis];
    }
    const Mat4 chrBase = CombineWithView(origin, modelviewF);
    if (camera.viewToWorld != nullptr && camera.worldToScreen != nullptr) {
        Mtxf worldToScreen;
        std::memcpy(worldToScreen.m, camera.worldToScreen, sizeof(worldToScreen.m));
        chrDraw_.SetView(*camera.viewToWorld, worldToScreen);
    }

    const Doors::Timing doorTiming{ chrs_.Timing().clockTimer, chrs_.Timing().globalTimer };
    // chrobjWeaponTick returns in every pass but the first player's.
    weaponTickPass_ = camera.position == 0;
    // objTick's isSimOwner: whether this pass advances the object's shared
    // state (its flight, doors and vehicles moving, sounds), which a frame does
    // once rather than for every player. A projectile belongs to the pass of
    // the player who fired it.
    const auto simOwner = [&](bool hasProjectile, int projectile) {
        if (camera.playerCount == 1) {
            return true;
        }
        bool owner = camera.position == 0;
        if (hasProjectile && projectile >= 0) {
            if (const int ownerProp = objects_.ProjectileAt(projectile).ownerProp; ownerProp >= 0) {
                owner = ownerProp == camera.playerProp;
            }
        }
        return owner;
    };
    // The tail of objTick every exit of an object's tick shares: the fuse and
    // mine tick, the damage a fall does, and letting go of what the object held.
    const auto finishObject = [&](int prop) {
        if (projectiles_ == nullptr) {
            return;
        }
        if (weaponTickPass_) {
            projectiles_->WeaponTick(prop);
        }
        if (PlacedObject* object = objects_.ForProp(prop);
            object != nullptr && (object->runtimeFlags & ObjectRuntime::DamageOnLand) != 0) {
            object->runtimeFlags &= ~ObjectRuntime::DamageOnLand;
            projectiles_->LandingDamage(*object);
        }
        projectiles_->DropRecursively(prop);
    };
    const auto tickInner = [&](int prop) -> TickOp {
        Prop& record = props[prop];
        bool owner = simOwner(false, -1);
        // objTick's door branch.
        if (record.type == PropType::Door && record.object >= 0) {
            if (projectiles_ != nullptr) {
                const PlacedDoor* falling = doors_.ForProp(prop);
                if (falling != nullptr &&
                    (falling->runtimeFlags & (ObjectRuntime::HasProjectile | ObjectRuntime::IsRetick)) != 0) {
                    owner = simOwner((falling->runtimeFlags & ObjectRuntime::HasProjectile) != 0, falling->projectile);
                    const TickOp op = projectiles_->TickDoor(prop, owner);
                    if (op == TickOp::Retick) {
                        return op;
                    }
                }
            }
            if (owner) {
                doors_.Tick(record.object, doorTiming);
            }
            if (effects_.playerPosition) {
                PlacedDoor* window = doors_.ForProp(prop);
                const PropModel* windowModel = doors_.Model(window->model);
                const bool broken = windowModel != nullptr && windowModel->skeleton == "door" &&
                                    std::find(window->hiddenSwitches.begin(), window->hiddenSwitches.end(), 1) !=
                                        window->hiddenSwitches.end();
                doors_.TickWindow(window, effects_.playerPosition(), broken,
                                  !effects_.windowPortalsFollowPlayer || effects_.windowPortalsFollowPlayer());
            }
        }
        if (record.type == PropType::Explosion || record.type == PropType::Smoke) {
            if (effects_.tick) {
                effects_.tick(prop);
            }
            return TickOp::None;
        }
        // The player's prop ticks as a character while it has a body
        // (playerTick), and as the viewer otherwise.
        const bool playerBody = record.type == PropType::Viewer && record.object >= 0;
        if ((record.type == PropType::Chr || playerBody) && record.object >= 0) {
            PlayerBodyMode mode = PlayerBodyMode::Tick;
            if (playerBody && playerBody_.before) {
                mode = playerBody_.before(prop);
            }
            if (mode == PlayerBodyMode::Hidden) {
                record.flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
                return TickOp::None;
            }
            const ChrDraw::Advanced advanced = chrDraw_.Advance(
                prop,
                [&](const Prop& chrProp, float radius) { return PosIsOnScreen(chrProp, radius, visibility, camera); },
                playerBody && mode == PlayerBodyMode::Tick);
            if (advanced.freed) {
                return TickOp::None;
            }
            chrDraw_.Pose(prop, advanced.visible, chrBase, camera.lodScaleZ);
            if (playerBody && playerBody_.after) {
                playerBody_.after(prop, mode);
            }
            return TickOp::None;
        }
        const bool isObject = (record.type == PropType::Obj || record.type == PropType::Weapon) && record.object >= 0;
        if (isObject) {
            if (const PlacedObject* removed = objects_.ForProp(prop);
                removed != nullptr && (removed->runtimeFlags & ObjectRuntime::Remove) != 0) {
                // objTick frees an object that was removed.
                if (effects_.freeObject) {
                    effects_.freeObject(prop);
                }
                objects_.FreeOrRespawn(prop);
                return TickOp::None;
            }
            if (projectiles_ != nullptr) {
                const PlacedObject* flying = objects_.ForProp(prop);
                owner = flying != nullptr &&
                        simOwner((flying->runtimeFlags & ObjectRuntime::HasProjectile) != 0, flying->projectile);
                const TickOp op = projectiles_->TickFlight(prop, owner);
                if (op == TickOp::Retick) {
                    return op;
                }
                if (op == TickOp::ChangedList) {
                    // The object stuck to another: it is drawn with its parent.
                    finishObject(prop);
                    record.stan = kNoTile;
                    return op;
                }
            }
        }
        if (objectTick_ && owner && record.type == PropType::Obj) {
            if (PlacedObject* vehicle = objects_.ForProp(prop);
                vehicle != nullptr && (vehicle->type == static_cast<uint8_t>(PropDefType::Vehicle) ||
                                       vehicle->type == static_cast<uint8_t>(PropDefType::Aircraft) ||
                                       vehicle->type == static_cast<uint8_t>(PropDefType::Cctv) ||
                                       vehicle->type == static_cast<uint8_t>(PropDefType::Autogun))) {
                objectTick_(*vehicle);
            }
        }
        if ((record.flags & PropFlag::Enabled) == 0) {
            record.flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
            SetChildrenOnScreen(prop, false);
            if (isObject) {
                finishObject(prop);
            }
            return TickOp::None;
        }
        // Tinted glass is exempt from the near fog's cull.
        const PlacedObject* placed = isObject ? objects_.ForProp(prop) : nullptr;
        const bool tintedGlass = placed != nullptr && placed->type == static_cast<uint8_t>(PropDefType::TintedGlass);
        if (tintedGlass && effects_.playerPosition) {
            // The glass is more transparent the closer the player is; an opaque
            // window has its portal closed.
            PlacedObject* glass = objects_.ForProp(prop);
            glass->calculatedOpacity = GlassOpacity(glass->runtimePosition, effects_.playerPosition(),
                                                    glass->tintDistance, glass->cullDistance, glass->tintNearOpacity);
            if (glass->portal >= 0 && effects_.setPortal &&
                (!effects_.windowPortalsFollowPlayer || effects_.windowPortalsFollowPlayer())) {
                effects_.setPortal(glass->portal, glass->calculatedOpacity == 0xFF);
            }
        }
        // The tank the player sits in is posed whether or not it is on screen;
        // a hidden object, or one kept out of sight, never is.
        const bool drivenTank = placed != nullptr && placed->type == static_cast<uint8_t>(PropDefType::Tank) &&
                                drivenTank_ && drivenTank_() == prop;
        const bool hidden = placed != nullptr && ((placed->flags2 & ObjectFlag2::Hidden) != 0 ||
                                                  (placed->runtimeFlags & ObjectRuntime::Unnamed0x800) != 0);
        if (!drivenTank && (hidden || !PosIsOnScreen(record, ObjectRadius(record), visibility, camera, !tintedGlass))) {
            record.flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
            SetChildrenOnScreen(prop, false);
            if (isObject) {
                finishObject(prop);
            }
            return TickOp::None;
        }
        // update_color_shading: an object on screen eases its shading towards
        // its tile's.
        if (PlacedObject* object = isObject ? objects_.ForProp(prop) : nullptr; object != nullptr) {
            if (owner) {
                EaseShading(object->shadeColour, object->nextColour);
            }
        } else if (PlacedDoor* door = doors_.ForProp(prop); door != nullptr && owner) {
            EaseShading(door->shadeColour, door->nextColour);
        }
        record.flags |= PropFlag::OnScreen;
        SetChildrenOnScreen(prop, true);
        if (record.type == PropType::Door && record.object >= 0 && owner) {
            doors_.TickIrisSound(record.object);
        }
        if (placed != nullptr && placed->type == static_cast<uint8_t>(PropDefType::Tank) && tankPose_) {
            tankPose_(*objects_.ForProp(prop));
        }
        // -render_pos.m[3][2]: the prop's position under the same view its
        // model draws under, measured along the camera's own axis rather than
        // straight-line distance.
        const float relX = record.position[0] - modelPos[0];
        const float relY = record.position[1] - modelPos[1];
        const float relZ = record.position[2] - modelPos[2];
        record.zDepth =
            -(relX * modelviewF[0][2] + relY * modelviewF[1][2] + relZ * modelviewF[2][2] + modelviewF[3][2]);
        if (isObject) {
            finishObject(prop);
        }
        return TickOp::None;
    };
    const auto tickProp = [&](int prop) -> TickOp {
        const TickOp op = tickInner(prop);
        // The tail of objTick that isSimOwner guards: the doors' sounds and the
        // autoguns and aircraft.
        const bool owner = simOwner(false, -1);
        if (op == TickOp::None && owner && props[prop].type == PropType::Door && props[prop].object >= 0) {
            doors_.UpdateSoundVolume(props[prop].object);
        }
        if (op == TickOp::None && owner && objectPose_ && props[prop].type == PropType::Obj) {
            if (PlacedObject* craft = objects_.ForProp(prop);
                craft != nullptr && (craft->autogun || craft->type == static_cast<uint8_t>(PropDefType::Aircraft) ||
                                     craft->type == static_cast<uint8_t>(PropDefType::Vehicle))) {
                objectPose_(*craft, (props[prop].flags & PropFlag::OnScreen) != 0);
            }
        }
        return op;
    };
    // propsTick walks from the newest prop to the oldest, and carries on from the
    // prop before the one it ticked, so a prop added at the start of the list
    // during the walk is reached before it ends.
    for (int prop = props.ActiveTail(); prop >= 0;) {
        const int before = props.ActiveBefore(prop);
        const TickOp op = tickProp(prop);
        if (op == TickOp::ChangedList) {
            prop = before;
        } else if (op == TickOp::Retick) {
            // The prop is ticked again at the end of the walk (chrpropActivateThisFrame).
            int next = props.IsActive(prop) ? props.ActiveBefore(prop) : before;
            props.Delist(prop);
            props.ActivateThisFrame(prop);
            prop = next < 0 ? prop : next;
        } else {
            prop = props.IsActive(prop) ? props.ActiveBefore(prop) : before;
        }
    }
    if (projectiles_ != nullptr) {
        projectiles_->EndTick();
    }
    // chraiUpdateOnscreenPropCount: the enabled, on-screen props collected
    // newest first once the walk is over, then ordered by a selection sort that
    // takes the first of any equal zDepths.
    constexpr uint8_t kShown = PropFlag::Enabled | PropFlag::OnScreen;
    onScreen_.clear();
    for (int prop = props.ActiveTail(); prop >= 0; prop = props.ActiveBefore(prop)) {
        if ((props[prop].flags & kShown) == kShown) {
            onScreen_.push_back(prop);
        }
    }
    for (size_t i = 0; i < onScreen_.size(); i++) {
        int best = -1;
        float bestDepth = -4.2949673e9f;
        for (size_t j = i; j < onScreen_.size(); j++) {
            if (bestDepth < props[onScreen_[j]].zDepth) {
                bestDepth = props[onScreen_[j]].zDepth;
                best = static_cast<int>(j);
            }
        }
        if (best >= 0) {
            std::swap(onScreen_[i], onScreen_[static_cast<size_t>(best)]);
        }
    }
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
    if ((record.type == PropType::Chr || record.type == PropType::Viewer) && record.object >= 0) {
        return chrDraw_.Render(dl, resourceManager, prop, withAlpha, frame);
    }
    if (record.type == PropType::Explosion || record.type == PropType::Smoke) {
        if (!effects_.render || !withAlpha) {
            return dl;
        }
        dl = ScissorToProp(dl, frame, record, true);
        return effects_.render(dl, prop, record, withAlpha);
    }
    if ((record.type != PropType::Obj && record.type != PropType::Weapon) || record.object < 0) {
        // The viewer's own prop isn't drawn without a body.
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

    if (placed != nullptr) {
        Mtxf relative = placed->mtx;
        for (int axis = 0; axis < 3; axis++) {
            relative.m[3][axis] = placed->runtimePosition[axis] - modelPos[axis];
        }
        return DrawObjectModel(dl, resourceManager, *placed, CombineWithView(relative, modelviewF), render, withAlpha,
                               modelPos, modelviewF);
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
                                 bool withAlpha, const Vec3& modelPos, const Gu::MatrixF& modelviewF) {
    const PropModel* model = objects_.Model(placed.model);
    if (model == nullptr) {
        return dl;
    }
    const std::string path = "models/prop/P" + model->name + "Z";
    // A monitor's screens are the models' switch nodes; the game draws them
    // from a per-frame display list instead of the model's own.
    const bool isMonitor = placed.type == static_cast<uint8_t>(PropDefType::Monitor) ||
                           placed.type == static_cast<uint8_t>(PropDefType::MultiMonitor);
    Model::ModelDrawOverrides overrides;
    overrides.hiddenSwitchSlots = placed.hiddenSwitches;
    if (isMonitor && (render.passes & 1) != 0 && monitorTick_) {
        overrides.screens = [this, &placed](const ModelTree& tree) { return MonitorScreens(placed, tree); };
    }
    if (placed.cctv) {
        if (const CctvGeometry* geometry = objects_.CctvGeometryOf(placed)) {
            // The camera swings on its pivot.
            overrides.matrices = [&placed, geometry, base, &modelPos, &modelviewF](const ModelTree& tree) {
                std::vector<Mat4> matrices(tree.MatrixCount, base);
                if (matrices.size() > 1) {
                    matrices[1] = CctvPose(placed, *geometry, modelPos, modelviewF);
                }
                return matrices;
            };
        }
    }
    if (placed.type == static_cast<uint8_t>(PropDefType::Aircraft)) {
        if (const SwitchGeometry* geometry = objects_.SwitchGeometryOf(placed)) {
            // The rotor turns, and an animated aircraft is posed by its clip.
            overrides.matrices = [&placed, geometry, base, &modelPos, &modelviewF](const ModelTree& tree) {
                return AircraftMatrices(placed, *geometry, base, tree.MatrixCount, modelPos, modelviewF);
            };
        }
    }
    if (placed.type == static_cast<uint8_t>(PropDefType::Vehicle)) {
        if (const VehicleGeometry* geometry = objects_.VehicleGeometryOf(placed)) {
            // The wheels roll, and the front pair steers.
            overrides.matrices = [geometry, &ai = placed.ai, base](const ModelTree& tree) {
                std::vector<Mat4> matrices(tree.MatrixCount, base);
                Mtxf root{};
                std::memcpy(root.m, base.m, sizeof(root.m));
                const std::array<Mtxf, 5> pose = VehicleWheelPose(*geometry, ai, root);
                for (size_t i = 1; i < pose.size() && i < matrices.size(); i++) {
                    std::memcpy(matrices[i].m, pose[i].m, sizeof(matrices[i].m));
                }
                return matrices;
            };
        }
    }
    if (placed.autogun) {
        if (const SwitchGeometry* geometry = objects_.SwitchGeometryOf(placed)) {
            // The gun turns to where it points, and its barrels spin.
            overrides.matrices = [&placed, geometry, base, &modelPos, &modelviewF](const ModelTree& tree) {
                return AutogunMatrices(placed, *geometry, base, tree.MatrixCount, modelPos, modelviewF);
            };
            for (size_t flash = 0; flash < placed.autogun->flash.size(); flash++) {
                if (placed.autogun->flash[flash]) {
                    overrides.visibleGunfireSlots.push_back(static_cast<uint8_t>(5 + (flash * 2)));
                }
            }
            overrides.modelScale = placed.scale;
        }
    }
    if (placed.type == static_cast<uint8_t>(PropDefType::Tank)) {
        if (const TankGeometry* geometry = objects_.TankGeometryOf(placed)) {
            // The turret and gun are posed by where the player has turned them,
            // the switch under the muzzle stays off and its flash shows while the
            // tank fires.
            constexpr uint8_t kTankHiddenSlot = 8;
            constexpr uint8_t kTankFlashSlot = 7;
            overrides.hiddenSwitchSlots.push_back(kTankHiddenSlot);
            if (placed.tank.firing) {
                overrides.visibleGunfireSlots.push_back(kTankFlashSlot);
            }
            overrides.modelScale = placed.scale;
            overrides.matrices = [geometry, state = placed.tank, base](const ModelTree& tree) {
                std::vector<Mat4> matrices(tree.MatrixCount, base);
                Mtxf root{};
                std::memcpy(root.m, base.m, sizeof(root.m));
                const std::array<Mtxf, 5> pose = TankPose(*geometry, state, root);
                for (size_t i = 1; i < pose.size() && i < matrices.size(); i++) {
                    std::memcpy(matrices[i].m, pose[i].m, sizeof(matrices[i].m));
                }
                return matrices;
            };
        }
    }

    // A destroyed object that was blown out of shape draws its blackened model
    // without culling, and a blended one deepens with how destroyed it is.
    ModelRenderParams objectRender = render;
    const Model::NodeVertices* deformed = effects_.deformed ? effects_.deformed(placed) : nullptr;
    const int destroyedLevel = Objects::DestroyedLevel(placed);
    if (destroyedLevel == 0 || deformed == nullptr) {
        objectRender.cullMode = kCullBack;
        if (render.propType == kRenderPropSmoke) {
            objectRender.envColour &= 0xFFFFFF00u;
        }
    } else {
        objectRender.cullMode = kCullNone;
        if (render.propType == kRenderPropSmoke) {
            const uint32_t alpha = std::min(255, (destroyedLevel * 50) + 100);
            objectRender.envColour = (objectRender.envColour & 0xFFFFFF00u) | alpha;
        } else if (destroyedLevel > 0) {
            objectRender.envColour |= 0xFF00u;
        }
        overrides.vertices.push_back(*deformed);
    }
    std::optional<Model::ModelDraw> built =
        Model::BuildModelDraw(resourceManager, path, base, /*textures=*/true, /*prefix=*/{}, kNoRootOrigin, isMonitor,
                              &objectRender, &overrides);
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
        const Mat4* node =
            embedded->embedNode >= 0 ? draw.NodeMatrix(embedded->embedNode) : draw.SwitchMatrix(embedded->embedSlot);
        if (node == nullptr) {
            continue;
        }
        Mtxf nodeMatrix{};
        std::memcpy(nodeMatrix.m, node->m, sizeof(nodeMatrix.m));
        const Mtxf childMatrix = Multiply(nodeMatrix, embedded->embedment);
        Mat4 childBase{};
        std::memcpy(childBase.m, childMatrix.m, sizeof(childBase.m));
        dl = DrawObjectModel(dl, resourceManager, *embedded, childBase, render, withAlpha, modelPos, modelviewF);
    }
    return dl;
}

Mat4 PropRender::CctvPose(const PlacedObject& camera, const CctvGeometry& geometry, const Vec3& modelPos,
                          const Gu::MatrixF& modelviewF) {
    const Vec3 relative = { camera.runtimePosition[0] - modelPos[0], camera.runtimePosition[1] - modelPos[1],
                            camera.runtimePosition[2] - modelPos[2] };
    return CombineWithView(CctvSwivel(*camera.cctv, camera.mtx, relative, geometry.pivot), modelviewF);
}

std::vector<Model::ModelDrawOverrides::Screen> PropRender::MonitorScreens(const PlacedObject& placed,
                                                                          const ModelTree& tree) {
    // The screens animate as the monitor draws, in the sub-draw that draws the
    // object opaque (or the only one it has).
    constexpr uint8_t kCollisionListType = 0x18;
    uint32_t mask = 0;
    const bool single = placed.type == static_cast<uint8_t>(PropDefType::Monitor);
    const size_t count = single ? 1 : 4;
    for (size_t slot = 0; slot < count; slot++) {
        if (slot < tree.Switches.size() && tree.Switches[slot] != kNoNode &&
            tree.Nodes[static_cast<size_t>(tree.Switches[slot])].type == kCollisionListType) {
            mask |= 1u << slot;
        }
    }
    PlacedObject* object = objects_.ForProp(placed.prop);
    if (object == nullptr) {
        return {};
    }
    monitorTick_(*object, mask);

    std::vector<Model::ModelDrawOverrides::Screen> screens;
    for (size_t slot = 0; slot < object->monitor.screens.size() && slot < count; slot++) {
        if ((mask & (1u << slot)) == 0 || !effects_.imageSize) {
            continue;
        }
        const MonitorScreen& screen = object->monitor.screens[slot];
        const int offset = EffectImage::kMonitors + (screen.image * EffectImage::kEntrySize);
        const EffectImageSize image = effects_.imageSize(offset);
        if (image.width == 0) {
            continue;
        }
        const MonitorScreen::Quad quad = screen.MakeQuad(image.width, image.height);

        // The depth mode: none for a screen that is always in front, decal for
        // a fixed one, else a plain surface.
        int zbufferMode = 1;
        if ((object->flags2 & ObjectFlag2::AlwaysVisible) != 0) {
            zbufferMode = 0;
        } else if ((object->flags & (single || slot == 0 ? ObjectFlag::MonitorFixed
                                                         : (ObjectFlag::MonitorFixed | ObjectFlag::SpecialFunc))) !=
                   0) {
            zbufferMode = 8;
        }
        // A screen with any transparency blends.
        const int mode = screen.alpha < 255 ? 2 : 1;

        Model::ModelDrawOverrides::Screen drawn;
        drawn.slot = static_cast<uint8_t>(slot);
        drawn.s = quad.s;
        drawn.t = quad.t;
        drawn.rgba = quad.rgba;
        drawn.select = [offset, mode, zbufferMode,
                        intensity = image.format == EffectImage::kFormatIntensity](std::vector<Gfx>& commands) {
            std::array<Gfx, 16> buffer;
            Gfx* end = SelectEffectImage(buffer.data(), offset, mode, zbufferMode, intensity);
            commands.insert(commands.end(), buffer.data(), end);
        };
        screens.push_back(std::move(drawn));
    }
    return screens;
}

std::vector<Mat4> PropRender::AircraftMatrices(const PlacedObject& craft, const SwitchGeometry& geometry,
                                               const Mat4& base, size_t count, const Vec3& modelPos,
                                               const Gu::MatrixF& modelviewF) {
    std::vector<Mat4> matrices(count, base);
    if (craft.instance && craft.instance->Playback().anim != nullptr) {
        // subcalcmatrices: the clip poses the model about where its root stands,
        // here measured from the point the props draw about.
        ModelPlayback& playback = craft.instance->Playback();
        const Frigate::Vec3 world = playback.root.pos;
        playback.root.pos = { world.x - modelPos[0], world.y - modelPos[1], world.z - modelPos[2] };
        Mat4 view{};
        std::memcpy(view.m, modelviewF, sizeof(view.m));
        const std::vector<Mat4> posed = craft.instance->Pose(view);
        playback.root.pos = world;
        for (size_t i = 0; i < count && i < posed.size(); i++) {
            matrices[i] = posed[i];
        }
    }
    if (count < 2) {
        return matrices;
    }
    Mtxf body{};
    std::memcpy(body.m, matrices[1].m, sizeof(body.m));
    const RotorPose rotor = PoseRotor(geometry, craft.ai.rotorAngle, (craft.flags & ObjectFlag::InMotion) != 0, body);
    for (size_t slot = 2; slot < rotor.matrix.size(); slot++) {
        const int index = slot == 2 ? 2 : geometry.matrix[slot];
        if (rotor.posed[slot] && index >= 0 && static_cast<size_t>(index) < count) {
            std::memcpy(matrices[static_cast<size_t>(index)].m, rotor.matrix[slot].m, sizeof(Mat4::m));
        }
    }
    return matrices;
}

std::vector<Mat4> PropRender::AutogunMatrices(const PlacedObject& gun, const SwitchGeometry& geometry, const Mat4& base,
                                              size_t count, const Vec3& modelPos, const Gu::MatrixF& modelviewF) {
    std::vector<Mat4> matrices(count, base);
    Mtxf body = gun.mtx;
    for (size_t axis = 0; axis < 3; axis++) {
        body.m[3][axis] = gun.runtimePosition[axis] - modelPos[axis];
    }
    const AutogunPose pose = PoseAutogun(geometry, *gun.autogun, body, gun.scale);
    for (size_t slot = 1; slot < pose.matrix.size(); slot++) {
        const int index = slot <= 2 ? static_cast<int>(slot) : geometry.matrix[slot];
        if (pose.posed[slot] && index >= 0 && static_cast<size_t>(index) < matrices.size()) {
            matrices[static_cast<size_t>(index)] = CombineWithView(pose.matrix[slot], modelviewF);
        }
    }
    return matrices;
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
