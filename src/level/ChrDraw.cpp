#include "level/ChrDraw.h"

#include <algorithm>
#include <deque>
#include <string>

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

// HATTYPE.
enum class HatType { Other = -1, Beret, Bird, Peaked, Helmet, Furry, Moon };

// get_hat_model, by the name of the hat's model.
HatType HatTypeOf(const std::string& modelName) {
    if (modelName == "hatfurry" || modelName == "hatfurrybrown" || modelName == "hatfurryblack") {
        return HatType::Furry;
    }
    if (modelName == "hattbird" || modelName == "hattbirdbrown") {
        return HatType::Bird;
    }
    if (modelName == "hathelmet" || modelName == "hathelmetgrey") {
        return HatType::Helmet;
    }
    if (modelName == "hatmoon") {
        return HatType::Moon;
    }
    if (modelName == "hatberet" || modelName == "hatberetblue" || modelName == "hatberetred") {
        return HatType::Beret;
    }
    if (modelName == "hatpeaked") {
        return HatType::Peaked;
    }
    return HatType::Other;
}

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

ChrDraw::Visibility ChrDraw::Advance(int prop) {
    PropList& props = objects_.Props();
    Chr& chr = chrs_[props[prop].object];
    if (!chr.model) {
        return Visibility::Hidden;
    }

    // TODO(https://github.com/liz-baker/frigate/issues/216): chrlvActionTick,
    // which runs the character's AI list and action state (a character starts
    // in ACT_INIT and stays there, with the animation it spawned without).
    // TODO(https://github.com/liz-baker/frigate/issues/217): the CHRHIDDEN_REMOVE
    // clean-up, the held weapons' removal, and the action-dependent
    // animation steps (patrol, go-to-position and stand skip their update
    // when off screen).
    if ((chr.chrflags & ChrFlag::Hidden) != 0) {
        return Visibility::Hidden;
    }
    if ((chr.chrflags & ChrFlag::CullUsingHitbox) != 0) {
        UpdateAnim(props[prop].object);
        return Visibility::Always;
    }
    if ((chr.chrflags & ChrFlag::IgnoreAnimTranslation) != 0) {
        if (chr.model->Playback().anim != nullptr) {
            chr.model->Playback().TickAnim(chrs_.Timing().clockTimer, false);
        }
    } else {
        UpdateAnim(props[prop].object);
    }
    return Visibility::Test;
}

void ChrDraw::UpdateAnim(int slot) {
    PropList& props = objects_.Props();
    Chr& chr = chrs_[slot];
    Prop& prop = props[chr.prop];
    ModelPlayback& playback = chr.model->Playback();

    if ((chr.hidden & ChrHidden::Freeze) == 0) {
        chr.prevPosition = { playback.root.pos.x, playback.root.pos.y, playback.root.pos.z };
        // chrlvActionTick always starts an animation before this step, so
        // modelTickAnim never sees a model without one.
        // TODO(https://github.com/liz-baker/frigate/issues/217): that action tick.
        if (playback.anim != nullptr) {
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

    // TODO(https://github.com/liz-baker/frigate/issues/218): chrUpdateAimProperties,
    // which smooths the aim angles the combat states set.
    chr.drawList.clear();

    if (onScreen) {
        record.flags |= PropFlag::OnScreen;
        chr.chrflags |= ChrFlag::HasBeenOnScreen;
        // TODO(https://github.com/liz-baker/frigate/issues/136): the DK Mode
        // cheat's distance scale.
        // TODO(https://github.com/liz-baker/frigate/issues/212):
        // chrHandleJointPositioned, which records the joints' positions for
        // bullet hit tests as the matrices are built.
        if (chr.flinchCount >= 0) {
            chr.flinchCount = static_cast<int8_t>(chr.flinchCount + chrs_.Timing().clockTimer);
            if (chr.flinchCount >= kFlinchTicks) {
                chr.flinchCount = -1;
            }
        }

        model.lod.zScale = lodScaleZ;
        model.Pose(base);
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

    // TODO(https://github.com/liz-baker/frigate/issues/218): the held items a
    // CHRHIDDEN_DROP_HELD_ITEMS character lets fall.
    // TODO(https://github.com/liz-baker/frigate/issues/153): chrlvTriggerFireWeapon.
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
        }
        // TODO(https://github.com/liz-baker/frigate/issues/127):
        // explosionRenderBulletImpactOnProp for the weapons and hat the
        // character holds.
        // TODO(https://github.com/liz-baker/frigate/issues/212): the
        // withAlpha pass's bondviewTransformManyPosToViewMatrix, which keeps
        // the joints' view-space positions for bullet hit tests.
    }

    if (withAlpha) {
        chr.drawList.clear();
    }
    return dl;
}

} // namespace Frigate::Level
