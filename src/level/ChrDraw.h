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

// The characters' share of chrTick and chrRenderProp: animating and posing each
// character's model with the weapons it holds and the hat it wears, and drawing
// them as one joint list.
class ChrDraw {
  public:
    ChrDraw(Chrs& chrs, Objects& objects, const Fog& fog) : chrs_(chrs), objects_(objects), fog_(fog) {
    }

    // Starts a frame: the draws of the frame before last can go.
    void BeginFrame();

    // chrlvActionTick, which chrTick runs first for a character that is not
    // hidden (or is one the tick always runs for).
    void SetActionTick(std::function<void(Chr&)> tick) {
        actionTick_ = std::move(tick);
    }

    // What chrTick decides about a character's visibility once it has stepped
    // its animation.
    enum class Visibility {
        // CHRFLAG_HIDDEN: not drawn, and no on-screen test.
        Hidden,
        // Drawn whatever the on-screen test says (CHRFLAG_CULL_USING_HITBOX).
        Always,
        // Drawn when posIsOnScreen says so.
        Test,
    };

    // chrTick up to its on-screen test: steps the character's animation of the
    // character whose prop is `prop`.
    Visibility Advance(int prop);

    // chrTick after the on-screen test: `onScreen` is its result. Poses the
    // model, the held weapons and the hat under `base` (the transform above a
    // model's root: the view, at the origin the props draw relative to) and
    // sets the prop's ONSCREEN flag and depth. `lodScaleZ` is the player's
    // getPlayer_c_lodscalez.
    void Pose(int prop, bool onScreen, const Mat4& base, float lodScaleZ);

    // chrRenderProp: `withAlpha` selects the blended sub-draw.
    Gfx* Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, int prop, bool withAlpha,
                const PropFrame& frame);

  private:
    // chrUpdateAnim.
    void UpdateAnim(int slot);
    // chrRenderHeldWeapon's share of chrTick for the weapon in `hand`.
    void PoseHeldWeapon(Chr& chr, int hand, const Mat4& base);
    // The hat's part of chrTick; false when it hides the head's hair.
    bool PoseHat(Chr& chr, const Mat4& base);

    Chrs& chrs_;
    Objects& objects_;
    const Fog& fog_;
    std::function<void(Chr&)> actionTick_;
    // The draws of the last two frames: their commands point at the matrices
    // and lists each owns, which the interpreter reads after the frame's list
    // is built.
    std::array<std::deque<Model::JointDraw>, 2> draws_;
    size_t generation_ = 0;
};

} // namespace Frigate::Level
