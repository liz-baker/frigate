#pragma once

#include <array>
#include <memory>
#include <vector>

#include "level/PlayerMove.h"
#include "model/ModelInstance.h"

namespace Ship {
class ResourceManager;
}

namespace Frigate::Level {

// Bond's head (bondhead.c): the gait model's walking animation, whose root
// motion moves the player, and the smoothed head position, look and up the view
// takes from it.
class GaitHead final : public HeadMotion {
  public:
    // Builds the gait model (`player_gait_object`, embedded in the game) and
    // loads the idle, walking and sprinting clips. Null, after logging why, if a
    // clip is missing.
    static std::unique_ptr<GaitHead> Create(const std::shared_ptr<Ship::ResourceManager>& resourceManager);

    // bondviewMoveAnimationTick's share: picks the animation for `speed`,
    // advances it, and returns the head position (bheadUpdate).
    Vec3 Tick(float speed, float speedForwards, float speedSideways, float breathing,
              const MoveContext& context) override;

    // The height the head stands at (standheight).
    float StandHeight() const {
        return standHeight_;
    }
    // headlook and headup: the view's direction and up vector in the player's
    // frame.
    Vec3 Look() const override {
        return headLook_;
    }
    Vec3 Up() const override {
        return headUp_;
    }
    // standbodyoffset.
    Vec3 BodyOffset() const override {
        return standBodyOffset_;
    }
    // bheadGetBreathingValue.
    float BreathingValue(float breathing) const override;
    void SetAnimFlipFlag(bool flip) override {
        flipFlag_ = flip;
    }
    bool OnFastAnimation() const override {
        return headAnim_ == 1;
    }
    bool DeathAnimationFinished() const override;

  private:
    GaitHead() = default;

    // The two walking animations and their speed thresholds
    // (g_BondMoveAnimationSetup).
    struct MoveAnimation {
        const AnimationClip* clip = nullptr;
        float loopFrame = 0;
        float endFrame = 0;
        float speedMultiplier = 0;
        float threshold = 0;
    };

    void Init();
    void AdjustAnimation(float speed);
    void UpdateIdleRoll();
    void UpdatePos(const Vec3& velocity, const MoveContext& context);
    void UpdateRot(const Vec3& lookVelocity, const Vec3& upVelocity, const MoveContext& context);
    void SetDamp(float damp);
    void BindAnimation(int index, float startFrame);

    std::shared_ptr<Model::ModelInstance> model_;
    const AnimationClip* idle_ = nullptr;
    // g_bondviewBondDeathAnimations, in the table's order.
    std::vector<const AnimationClip*> deathClips_;
    // startnewbonddie.
    bool startNewDeath_ = true;
    std::array<MoveAnimation, 2> moves_;

    int headAnim_ = 0;
    float headDamp_ = 0.93f;
    int headWalkingTime60_ = 0;
    float headAmplitude_ = 1.0f;
    float sideAmplitude_ = 1.0f;
    Vec3 headPos_{};
    Vec3 headLook_{};
    Vec3 headUp_{};
    Vec3 headPosSum_{};
    Vec3 headLookSum_{};
    Vec3 headUpSum_{};
    bool resetHeadPos_ = true;
    bool resetHeadRot_ = true;
    float standHeight_ = 0;
    std::array<float, 3> standBodyOffset_{};
    float standFrac_ = 0;
    std::array<Vec3, 2> standLook_{};
    std::array<Vec3, 2> standUp_{};
    int standCount_ = 0;
    bool flipFlag_ = false;
};

} // namespace Frigate::Level
