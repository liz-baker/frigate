#pragma once

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include "gfx/Gu.h"
#include "level/Chrs.h"
#include "level/Fog.h"
#include "level/Objects.h"
#include "level/PropFog.h"
#include "libultraship/libultra/gbi.h"
#include "model/JointDraw.h"
#include "model/ModelWalk.h"

namespace Ship {
class ResourceManager;
} // namespace Ship

namespace Frigate::Level {

// explosionRenderBulletImpactOnProp: draws the bullet marks on `prop`, whose
// model's nodes have the view-space matrices `matrices`. `drawn` is set to
// whether it drew any.
using MarkDrawFn = std::function<Gfx*(Gfx* dl, int prop, const Mtx::FixedMtx* matrices, size_t matrixCount,
                                      bool withAlpha, bool* drawn)>;

// The characters' share of chrTick and chrRenderProp: animating and posing each
// character's model with the weapons it holds and the hat it wears, and drawing
// them as one joint list.
class ChrDraw {
  public:
    ChrDraw(Chrs& chrs, Objects& objects, const Fog& fog) : chrs_(chrs), objects_(objects), fog_(fog) {
    }

    // Starts a frame: the draws of the frame before last can go.
    // `newFrame` starts the frame's draws over; another player's pass of the
    // same frame adds to them.
    void BeginFrame(bool newFrame = true);

    // Draws the marks on the weapons and hat a character holds.
    void SetMarkDraw(MarkDrawFn draw) {
        markDraw_ = std::move(draw);
    }

    // chrlvActionTick, which chrTick runs first for a character that is not
    // hidden (or is one the tick always runs for).
    void SetActionTick(std::function<void(Chr&)> tick) {
        actionTick_ = std::move(tick);
    }

    // What a character's tick does to its fighting: chrUpdateAimProperties,
    // chrlvTriggerFireWeapon, objDrop for each item it lets fall and chrSetFiring
    // for a gun that leaves its hand.
    struct CombatHooks {
        std::function<void(Chr&)> updateAim;
        std::function<void(Chr&)> triggerFire;
        std::function<void(int prop)> dropObject;
        std::function<void(Chr&, int hand)> stopFiring;
        // chrHandleJointPositioned: turns the joint whose first matrix is
        // `index` towards where the character aims and away from a hit, given the
        // transforms between the view and the world.
        std::function<void(Chr&, int index, Mat4* matrix, const Mtxf& viewToWorld, const Mtxf& worldToScreen)>
            positionJoint;
        // cheatIsActive(CHEAT_DK_MODE).
        std::function<bool()> dkMode;
    };
    // The view the frame's characters are posed in.
    void SetView(const Mtxf& viewToWorld, const Mtxf& worldToScreen) {
        viewToWorld_ = viewToWorld;
        worldToScreen_ = worldToScreen;
    }
    void SetCombat(CombatHooks hooks) {
        combat_ = std::move(hooks);
    }

    // posIsOnScreen for a character: whether its prop, with a bounding
    // sphere of `radius`, is on screen.
    using OnScreenTest = std::function<bool(const Prop& prop, float radius)>;

    // What chrTick settles before it poses the character.
    struct Advanced {
        // The character was removed: its prop is freed (TICKOP_FREE).
        bool freed = false;
        // The character is drawn this frame (headSwitchVisible).
        bool visible = false;
    };

    // chrTick up to its on-screen result: runs the character's action tick,
    // steps its animation (which of the ways depends on its action) and
    // tests whether it is on screen with `onScreen`, in the order the action
    // asks for.
    // `alwaysVisible` is the cull a character carrying the viewer's cinematic
    // camera (or CHRFLAG_CULL_USING_HITBOX) takes: drawn whatever the view.
    Advanced Advance(int prop, const OnScreenTest& onScreen, bool alwaysVisible = false);

    // chrTick after the animation: `onScreen` is its result. Poses the
    // model, the held weapons and the hat under `base` (the transform above a
    // model's root: the view, at the origin the props draw relative to) and
    // sets the prop's ONSCREEN flag and depth. `lodScaleZ` is the player's
    // getPlayer_c_lodscalez.
    void Pose(int prop, bool onScreen, const Mat4& base, float lodScaleZ);

    // chrRenderProp: `withAlpha` selects the blended sub-draw.
    Gfx* Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, int prop, bool withAlpha,
                const PropFrame& frame);

    // chrpropCleanupForRemoval: the character of `prop` stops, lets go of the
    // props it holds and frees its slot. The prop itself stays, with no
    // character (the player's, when Bond's body goes).
    void CleanupChr(int prop);

  private:
    // chrUpdateAnim: steps the animation (unless `tick` is false) and moves the
    // character with it.
    void UpdateAnim(int slot, bool tick);
    // modelTickAnim without the root motion.
    void TickAnimOnly(Chr& chr);
    // chrpropCleanupForRemoval and the TICKOP_FREE that follows: the
    // character, the props it holds and its prop go.
    void FreeChr(int prop);
    // objDetach and objFreePermanently for a prop the character holds.
    void FreeHeld(Chr& chr, int heldProp);
    // chrRenderHeldWeapon's share of chrTick for the weapon in `hand`.
    void PoseHeldWeapon(Chr& chr, int hand, const Mat4& base);
    // The hat's part of chrTick; false when it hides the head's hair.
    bool PoseHat(Chr& chr, const Mat4& base);

    Chrs& chrs_;
    Objects& objects_;
    const Fog& fog_;
    std::function<void(Chr&)> actionTick_;
    CombatHooks combat_;
    Mtxf viewToWorld_ = IdentityMtxf();
    Mtxf worldToScreen_ = IdentityMtxf();
    MarkDrawFn markDraw_;
    // The draws of the last two frames: their commands point at the matrices
    // and lists each owns, which the interpreter reads after the frame's list
    // is built.
    std::array<std::deque<Model::JointDraw>, 2> draws_;
    size_t generation_ = 0;
};

} // namespace Frigate::Level
