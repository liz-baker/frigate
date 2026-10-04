#include "level/ChrDraw.h"

#include <algorithm>
#include <deque>
#include <string>

#include "level/HatType.h"
#include "level/LevelModels.h"
#include "level/Matrix.h"
#include "level/Shading.h"
#include "model/RenderState.h"

namespace Frigate::Level {

namespace {

// The hat placement's offsets are in units of this many model units.
constexpr float kHatOffsetUnit = 21.3f;
// Hats per male head in the placement table (HATTYPE_BERRET to HATTYPE_MOON).
constexpr int kHatsPerHead = 6;
// The switch of a head model that hides its hair, which a peaked hat covers.
constexpr size_t kHeadHairSwitch = 1;
// flinchcnt counts up to this many ticks (NTSC) before it stops.
constexpr int kFlinchTicks = 30;
// The shadow level chrRenderProp sets (sub_GAME_7F073FC8) with no fog to fade it.
constexpr int32_t kDefaultShadowLevel = 0x50;
// gBloodColour.
constexpr uint32_t kBloodColour = 0x5A000000;

// The instance behind a level model handle.
Model::ModelInstance& Instance(ModelHandle& handle) {
    return *static_cast<LevelModel&>(handle).Instance();
}

Mat4 ToMat4(const Mtxf& matrix) {
    Mat4 out{};
    std::copy(&matrix.m[0][0], &matrix.m[0][0] + 16, &out.m[0][0]);
    return out;
}

} // namespace

void ChrDraw::BeginFrame() {
    generation_ ^= 1;
    draws_[generation_].clear();
}

ChrDraw::Advanced ChrDraw::Advance(int prop, const OnScreenTest& onScreen, bool alwaysVisible) {
    PropList& props = objects_.Props();
    const int slot = props[prop].object;
    Chr& chr = chrs_[slot];
    Advanced result;

    if (((chr.chrflags & ChrFlag::Hidden) == 0 || (chr.chrflags & ChrFlag::Unnamed0x40000) != 0) && actionTick_) {
        actionTick_(chr);
    }
    // A source with no models has characters that only take up their props.
    if (!chr.model) {
        return result;
    }

    if ((chr.hidden & ChrHidden::Remove) != 0) {
        FreeChr(prop);
        result.freed = true;
        return result;
    }

    for (const int hand : { HandRight, HandLeft }) {
        const int held = chr.weaponsHeld[static_cast<size_t>(hand)];
        const PlacedObject* weapon = held >= 0 ? objects_.ForProp(held) : nullptr;
        if (weapon != nullptr && (weapon->runtimeFlags & ObjectRuntime::Remove) != 0) {
            FreeHeld(chr, held);
        }
    }

    if ((chr.chrflags & ChrFlag::Hidden) != 0) {
        return result;
    }

    ModelPlayback& playback = chr.model->Playback();
    const float radius = chr.boundingRadius * playback.scale;
    const auto onScreenNow = [&]() { return onScreen(props[prop], radius); };
    const ChrTiming& timing = chrs_.Timing();

    if (alwaysVisible || (chr.chrflags & ChrFlag::CullUsingHitbox) != 0) {
        result.visible = true;
        if (chr.actionType == ChrAction::Anim && !chr.actAnim.unnamed2C && chr.actAnim.noTranslate) {
            TickAnimOnly(chr);
        } else {
            UpdateAnim(slot, true);
        }
    } else if (chr.actionType == ChrAction::Patrol || chr.actionType == ChrAction::GoPos) {
        const bool patrol = chr.actionType == ChrAction::Patrol;
        const Chr::WayData& way = patrol ? chr.actPatrol.way : chr.actGoPos.way;
        if (way.mode == WayMode::Magic) {
            // A character moved along its route unseen is only posed once it is on screen.
            result.visible = onScreenNow();
            if (result.visible) {
                UpdateAnim(slot, false);
            }
        } else {
            UpdateAnim(slot, true);
            result.visible = onScreenNow();
            if (result.visible) {
                (patrol ? chr.actPatrol.lastVisible60 : chr.actGoPos.lastVisible60) = timing.globalTimer;
            }
        }
    } else if (chr.actionType == ChrAction::Anim && !chr.actAnim.unnamed2C) {
        result.visible = onScreenNow();
        if (result.visible && !chr.actAnim.noTranslate) {
            UpdateAnim(slot, true);
        } else {
            TickAnimOnly(chr);
        }
    } else if (chr.actionType == ChrAction::Stand) {
        result.visible = onScreenNow();
        if (result.visible || (chr.chrflags & ChrFlag::Init) != 0) {
            UpdateAnim(slot, true);
        } else if (playback.anim2 != nullptr) {
            TickAnimOnly(chr);
        }
    } else {
        if ((chr.chrflags & ChrFlag::IgnoreAnimTranslation) != 0) {
            TickAnimOnly(chr);
        } else {
            UpdateAnim(slot, true);
        }
        result.visible = onScreenNow();
    }
    return result;
}

void ChrDraw::TickAnimOnly(Chr& chr) {
    ModelPlayback& playback = chr.model->Playback();
    if (playback.anim != nullptr) {
        playback.TickAnim(chrs_.Timing().clockTimer, false);
    }
}

void ChrDraw::UpdateAnim(int slot, bool tick) {
    PropList& props = objects_.Props();
    Chr& chr = chrs_[slot];
    Prop& prop = props[chr.prop];
    ModelPlayback& playback = chr.model->Playback();

    if ((chr.hidden & ChrHidden::Freeze) == 0) {
        chr.prevPosition = { playback.root.pos.x, playback.root.pos.y, playback.root.pos.z };
        // The action tick starts an animation before this step, so
        // modelTickAnim never sees a model without one.
        if (tick && playback.anim != nullptr) {
            playback.TickAnim(chrs_.Timing().clockTimer, true);
        }
        playback.CalcPos();
        chr.nextColour = chrs_.TileShading(prop.stan);
        prop.position = { playback.root.pos.x, playback.root.pos.y, playback.root.pos.z };
        chrs_.DetectRooms(slot);
        return;
    }

    playback.CalcPos();
    prop.position = { playback.root.pos.x, playback.root.pos.y, playback.root.pos.z };
}

void ChrDraw::FreeHeld(Chr& chr, int heldProp) {
    PropList& props = objects_.Props();
    // objDetach.
    props.Detach(heldProp);
    if (PlacedObject* object = objects_.ForProp(heldProp)) {
        object->runtimeFlags &= ~ObjectRuntime::HasOwner;
    }
    if (heldProp == chr.hat) {
        chr.hat = -1;
    } else if (heldProp == chr.weaponsHeld[HandRight]) {
        if (combat_.stopFiring) {
            combat_.stopFiring(chr, HandRight);
        }
        chr.weaponsHeld[HandRight] = -1;
    } else if (heldProp == chr.weaponsHeld[HandLeft]) {
        if (combat_.stopFiring) {
            combat_.stopFiring(chr, HandLeft);
        }
        chr.weaponsHeld[HandLeft] = -1;
    }
    // objFreePermanently.
    objects_.Free(heldProp);
}

void ChrDraw::CleanupChr(int prop) {
    PropList& props = objects_.Props();
    Chr& chr = chrs_[props[prop].object];

    for (std::array<Game::Sound::Handle, 2>& hand : chr.gunSounds) {
        hand[0].StopIfPlaying();
        hand[1].StopIfPlaying();
    }
    props.DeregisterRooms(prop);
    for (int child = props[prop].child; child >= 0;) {
        const int next = props[child].prev;
        FreeHeld(chr, child);
        child = next;
    }

    chr.model.reset();
    chr.drawList.clear();
    chr.chrnum = -1;
    chr.inUse = false;
    chr.prop = -1;
    props[prop].object = -1;
}

void ChrDraw::FreeChr(int prop) {
    PropList& props = objects_.Props();
    CleanupChr(prop);

    // propExecuteTickOperation(TICKOP_FREE).
    props.DeregisterRooms(prop);
    props.Delist(prop);
    props.Disable(prop);
    props.Free(prop);
    props[prop].type = PropType::Nul;
}

void ChrDraw::PoseHeldWeapon(Chr& chr, int hand, const Mat4& base) {
    const int weaponProp = chr.weaponsHeld[static_cast<size_t>(hand)];
    if (weaponProp < 0) {
        return;
    }
    PropList& props = objects_.Props();
    const PlacedObject* weapon = objects_.ForProp(weaponProp);
    Prop& record = props[weaponProp];
    if (weapon == nullptr || (weapon->runtimeFlags & ObjectRuntime::Unnamed0x800) != 0 ||
        (weapon->flags2 & ObjectFlag2::Unnamed0x80000) != 0) {
        record.flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
        return;
    }
    record.flags |= PropFlag::OnScreen;
    if (weapon->instance) {
        Instance(*weapon->instance).Pose(base);
        if ((weapon->runtimeFlags & ObjectRuntime::HasProjectile) == 0) {
            chr.drawList.push_back(weapon->instance);
        }
    }
}

bool ChrDraw::PoseHat(Chr& chr, const Mat4& base) {
    bool headVisible = true;
    PropList& props = objects_.Props();
    const PlacedObject* hat = objects_.ForProp(chr.hat);
    if (hat == nullptr) {
        return headVisible;
    }
    props[chr.hat].flags |= PropFlag::OnScreen;
    if (!hat->instance) {
        return headVisible;
    }
    Model::ModelInstance& hatInstance = Instance(*hat->instance);
    hatInstance.Pose(base);

    const int headStart = chrs_.HeadStart();
    if (chr.headnum >= headStart && chr.headnum < chrs_.FemaleHeadStart() && headStart >= 0 &&
        chrs_.Hats() != nullptr) {
        const PropModel* model = objects_.Model(hat->model);
        const HatType type = model != nullptr ? HatTypeOf(model->name) : HatType::Other;
        // headHat_array_8003E464[headindex][hat]. A hat that is none of the
        // named types indexes the entry before the head's own.
        const size_t index = static_cast<size_t>(((chr.headnum - headStart) * kHatsPerHead) + static_cast<int>(type));
        if (const Game::HatPlacement* entry = chrs_.Hats()->At(index)) {
            Mtxf placement = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } };
            placement.m[3][0] = entry->xoffset * kHatOffsetUnit;
            placement.m[3][1] = entry->yoffset * kHatOffsetUnit;
            placement.m[3][2] = entry->zoffset * kHatOffsetUnit;
            ScaleRowBasis(&placement, 0, entry->xsize);
            ScaleRowBasis(&placement, 1, entry->ysize);
            ScaleRowBasis(&placement, 2, entry->zsize);
            hatInstance.PrependToMatrix0(ToMat4(placement));
            if (type == HatType::Peaked) {
                headVisible = false;
            }
        }
    }

    if ((chr.hidden & ChrHidden::DropHeldItems) == 0 || (hat->runtimeFlags & ObjectRuntime::HasProjectile) == 0) {
        chr.drawList.push_back(hat->instance);
    }
    return headVisible;
}

void ChrDraw::Pose(int prop, bool onScreen, const Mat4& base, float lodScaleZ) {
    PropList& props = objects_.Props();
    Prop& record = props[prop];
    Chr& chr = chrs_[record.object];
    if (!chr.model) {
        record.flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
        return;
    }
    Model::ModelInstance& model = Instance(*chr.model);

    if (chr.actionType != ChrAction::Stand || model.playback.anim2 != nullptr) {
        chr.hidden |= ChrHidden::BackgroundAi;
    }

    if (combat_.updateAim) {
        combat_.updateAim(chr);
    }
    chr.drawList.clear();

    if (onScreen) {
        record.flags |= PropFlag::OnScreen;
        chr.chrflags |= ChrFlag::HasBeenOnScreen;
        // modelSetDistanceScale: DK Mode picks the detail levels as if the
        // character were nearer; the scale is back to 1 once the pose is made.
        model.lod.distanceScale = combat_.dkMode && combat_.dkMode() ? 0.3125f : 1.0f;
        if (chr.flinchCount >= 0) {
            chr.flinchCount = static_cast<int8_t>(chr.flinchCount + chrs_.Timing().clockTimer);
            if (chr.flinchCount >= kFlinchTicks) {
                chr.flinchCount = -1;
            }
        }

        model.lod.zScale = lodScaleZ;
        if (combat_.positionJoint) {
            model.jointHook = [this, &chr](uint32_t index, Mat4* matrix) {
                combat_.positionJoint(chr, static_cast<int>(index), matrix, viewToWorld_, worldToScreen_);
            };
        }
        model.Pose(base);
        model.jointHook = nullptr;
        model.lod.distanceScale = 1.0f;
        EaseShading(chr.shadeColour, chr.nextColour);
        const Mat4* root = FindNodeMatrix(model.Nodes(), model.Walk(), 0);
        record.zDepth = root != nullptr ? -root->m[3][2] : 0.0f;

        chr.drawList.push_back(chr.model);
        PoseHeldWeapon(chr, HandRight, base);
        PoseHeldWeapon(chr, HandLeft, base);
        bool headVisible = true;
        if (chr.hat >= 0) {
            headVisible = PoseHat(chr, base);
        }
        model.SetHeadSwitchVisible(kHeadHairSwitch, headVisible);
    } else {
        for (const int held : { chr.weaponsHeld[HandRight], chr.weaponsHeld[HandLeft], chr.hat }) {
            if (held >= 0) {
                props[held].flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
            }
        }
        record.flags &= static_cast<uint8_t>(~PropFlag::OnScreen);
        chr.shadeColour = chr.nextColour;
    }

    if ((chr.chrflags & ChrFlag::Hidden) == 0) {
        if ((chr.hidden & ChrHidden::DropHeldItems) != 0) {
            // objDrop for each item, newest first; the item leaves the list as it drops.
            int drop = record.child;
            while (drop >= 0) {
                const int next = props[drop].prev;
                if (combat_.dropObject) {
                    combat_.dropObject(drop);
                }
                drop = next;
            }
            chr.hidden &= ~ChrHidden::DropHeldItems;
        }

        if (combat_.triggerFire) {
            combat_.triggerFire(chr);
        }
    }
}

Gfx* ChrDraw::Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, int prop, bool withAlpha,
                     const PropFrame& frame) {
    PropList& props = objects_.Props();
    const Prop& record = props[prop];
    Chr& chr = chrs_[record.object];
    if (!chr.model) {
        return dl;
    }

    int fadeAlpha = chr.fadeAlpha;
    if ((chr.chrflags & ChrFlag::NoFade) == 0) {
        const float size = chr.boundingRadius * chr.model->Playback().scale;
        fadeAlpha =
            static_cast<int>(FogVisibility(fog_, record.zDepth, size, frame.lodScaleZ) * static_cast<float>(fadeAlpha));
    }

    uint32_t passes;
    if (fadeAlpha < 0xFF || (chr.chrflags & ChrFlag::Unnamed0x20000) != 0) {
        if (!withAlpha) {
            return dl;
        }
        passes = 3;
    } else {
        passes = withAlpha ? 2 : 1;
    }

    FogColour fogColour{};
    const int fogResult = fog_.PropDistColour(record.zDepth, &fogColour);
    if (fogResult != Fog::kPropHidden && fadeAlpha > 0) {
        dl = ScissorToProp(dl, frame, record, (chr.chrflags & ChrFlag::CullUsingHitbox) == 0);

        ModelRenderParams params;
        params.zbufferEnabled = true;
        params.passes = passes;
        params.shadowLevel = kDefaultShadowLevel;
        if ((chr.chrflags & ChrFlag::NoShadow) != 0) {
            params.shadowLevel = 0;
        } else if (fogResult == Fog::kPropFogged) {
            params.shadowLevel = static_cast<int32_t>((1.0f - fogColour[3]) * static_cast<float>(params.shadowLevel));
        }
        IntColour shade = { chr.shadeColour[0], chr.shadeColour[1], chr.shadeColour[2], chr.shadeColour[3] };
        LerpTowardsFog(shade, fogResult, fogColour);
        params.envColour = kBloodColour;
        params.fogColour = PackColour(shade);
        // The decomp stores 7, or 8 for a faded character; its render setup
        // compares against the prop type plus one, so those are the viewer's
        // and the explosion's.
        if (fadeAlpha < 0xFF) {
            params.propType = kRenderPropExplosion;
            params.envColour |= static_cast<uint32_t>(fadeAlpha);
        } else {
            params.propType = kRenderPropViewer;
        }

        chrs_.CountDrawn();
        std::vector<std::shared_ptr<Model::ModelInstance>> models;
        for (const auto& handle : chr.drawList) {
            models.push_back(static_cast<LevelModel&>(*handle).Instance());
        }
        auto draw = Model::BuildJointDraw(resourceManager, models, params);
        if (draw) {
            Model::JointDraw& kept = draws_[generation_].emplace_back(std::move(*draw));
            for (const Gfx& command : kept.commands) {
                *dl++ = command;
            }
            // The marks on the weapons and the hat.
            if (markDraw_) {
                for (const int held : { chr.weaponsHeld[HandRight], chr.weaponsHeld[HandLeft], chr.hat }) {
                    PlacedObject* object = held >= 0 ? objects_.ForProp(held) : nullptr;
                    if (object == nullptr || !object->instance || (object->state & (1u << (withAlpha ? 1 : 0))) == 0) {
                        continue;
                    }
                    const auto at = std::find(chr.drawList.begin(), chr.drawList.end(), object->instance);
                    if (at == chr.drawList.end()) {
                        continue;
                    }
                    const auto& matrices = kept.matrices[static_cast<size_t>(at - chr.drawList.begin())];
                    bool drawn = false;
                    dl = markDraw_(dl, held, matrices.data(), matrices.size(), withAlpha, &drawn);
                    if (!drawn) {
                        object->state &= static_cast<uint8_t>(~(1u << (withAlpha ? 1 : 0)));
                    }
                }
            }
        }
    }

    if (withAlpha) {
        chr.drawList.clear();
    }
    return dl;
}

} // namespace Frigate::Level
