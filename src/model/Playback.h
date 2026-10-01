#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "model/Animation.h"
#include "model/ModelWalk.h"

// A character model's animation playback: which clip is playing at what
// frame and speed, the optional secondary clip it is merging from, and the
// root-motion state that turns the clip's position/heading channels into where
// the model stands. It's the GoldenEye decomp's Model playback fields and its
// header node's read-write record, without the graphics: nothing here
// depends on libultraship, so it runs (and is tested) without a window.
//
// Field names follow how the decomp uses each field; its own name is in
// parentheses where it differs materially (unkNN), so the port can be checked
// against model.c line by line.
namespace Frigate {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// The header (root) node's read-write record.
struct RootState {
    bool motionDisabled = false; // (unk00) skips root motion; the frame is applied as-is
    bool hasTarget = false;      // (unk01) `targetPos`/`targetHeading` are valid
    bool hasMergeSource = false; // (unk02) `mergeStartPos`/`mergeTargetPos` are valid
    float ground = 0.0f;
    Vec3 pos;                           // where the model stands
    float heading = 0.0f;               // (unk14) radians
    float headingOverrideBlend = 0.0f;  // (unk18) 0 = use `heading`
    float headingOverride = 0.0f;       // (unk1c)
    float targetHeading = 0.0f;         // (unk20) heading at the end of the current frame step
    Vec3 targetPos;                     // (unk24) position at the end of the current frame step
    float startHeading = 0.0f;          // (unk30)
    Vec3 startPos;                      // (unk34) position at the start of the current frame step
    Vec3 mergeTargetPos;                // (unk40) `targetPos` of the clip being merged from
    Vec3 mergeStartPos;                 // (unk4c) `startPos` of the clip being merged from
    float headingCorrection = 0.0f;     // (unk58)
    float headingCorrectionTime = 0.0f; // (unk5c)
};

struct ModelPlayback {
    // The model's joint table and root header node's joint (the decomp's
    // AnimPart). `hasRoot` is false for a model whose root node is not a
    // header node; such a model has playback but no root motion.
    bool hasRoot = false;
    const std::vector<JointChannels>* joints = nullptr;
    uint16_t rootJoint = 0;
    RootState root;

    float scale = 1.0f;
    float animTranslationScale = 1.0f;

    const AnimationClip* anim = nullptr;
    bool mirror = false;  // (gunhand) use each joint's mtxB channel
    bool mirror2 = false; // (unk25) `mirror` for `anim2`
    bool animLooping = false;
    float frame = 0.0f;     // (animframe1) fractional frame
    float frameFrac = 0.0f; // (unk2c) `frame`'s fraction, the blend weight of frameB over frameA
    int16_t frameA = 0;
    int16_t frameB = 0;
    float endFrame = -1.0f; // negative: play to the clip's last frame

    float speed = 1.0f;
    float newSpeed = 0.0f;
    float oldSpeed = 0.0f;
    float timeSpeed = 0.0f;
    float elapseSpeed = 0.0f;

    // The clip merged from, set by CopyAnimForMerge.
    const AnimationClip* anim2 = nullptr;
    float frame2 = 0.0f;     // (animframe2)
    float frame2Frac = 0.0f; // (unk5c)
    int16_t frame2A = 0;
    int16_t frame2B = 0;
    float endFrame2 = -1.0f; // (unk6c)
    float speed2 = 1.0f;
    float newSpeed2 = 0.0f;    // (unk74)
    float oldSpeed2 = 0.0f;    // (unk78)
    float timeSpeed2 = 0.0f;   // (unk7c)
    float elapseSpeed2 = 0.0f; // (unk80)

    float mergeWeight = 0.0f;   // (unk84) weight of `anim2`
    float mergeDuration = 0.0f; // (unk88)
    float mergeElapsed = 0.0f;  // (unk8c)

    float animLoopFrame = 0.0f;
    float animLoopMerge = 0.0f;
    std::function<void()> animFlipFunc;

    // Called as the root moves: given where it was and where it wants to be,
    // may adjust the destination and the ground height, or veto the move by
    // returning false. Optional.
    std::function<bool(const Vec3& from, Vec3& to, float& ground)> moveFilter; // (unka0)

    float playSpeed = 1.0f;
    float animRate = 0.0f;             // target of a play-speed ramp
    float oldPlaySpeed = 0.0f;         // (unkac)
    float playSpeedRampTime = 0.0f;    // (unkb0)
    float playSpeedRampElapsed = 0.0f; // (unkb4)

    // Playback control, as the decomp's model* functions.
    void SetAnimation(const AnimationClip* clip, bool flip, float startFrame, float speed, float merge);
    void SetAnimationWithMerge(const AnimationClip* clip, bool flip, float startFrame, float speed, float merge,
                               bool doMerge);
    void SetAnimLooping(float loopFrame, float loopMerge);
    void SetAnimEndFrame(float endFrame);
    void SetAnimSpeed(float newSpeed, float rampTime);
    // sub_GAME_7F06FE90: ramps the speed over `rampTime` ticks so the clip
    // reaches `targetFrame` (wrapping past the end if it is behind).
    void RampSpeedToFrame(float targetFrame, float rampTime);
    void SetAnimPlaySpeed(float rate, float rampTime);
    void SetAnimFrame(float frame);
    void SetAnimFrame2(float frame, float frame2);

    // modelGetAnimEndFrame: the frame the clip plays to (the clip's last when no
    // end was set, 0 without a clip).
    float AnimEndFrame() const;

    // Advances playback by `ticks`. `updateRoot` also steps the root motion
    // through every frame crossed (the decomp's update_chrstuff).
    void TickAnim(int32_t ticks, bool updateRoot);

    // Applies this frame's root motion to the model's position and heading.
    void CalcPos();

    // Places the model: setsuboffset / setsubroty.
    void SetPosition(const Vec3& position);
    void SetHeading(float angle);

    // Shared by the two entry points above and by TickAnim.
    void CopyAnimForMerge(float mergeTime);
    void SetAnimation2(const AnimationClip* clip, bool flip, float startFrame, float speed, float merge);
    void SetAnimFrame2WithRoot(float frameA, float frameB, float frame2A, float frame2B);

  private:
    int32_t ConstrainOrWrapFrame(int32_t frame, const AnimationClip* clip, float endFrame) const;
};

// What WalkModel needs from a playback to pose the model this frame: the
// frames and weights to decode (the merged-from clip only while its weight is
// non-zero), and where the root stands.
AnimationPose MakePose(const ModelPlayback& playback);
RootPlacement MakePlacement(const ModelPlayback& playback);

// Whether root motion blends the merged-from clip's movement in
// (modelSetAnimMergingEnabled); on by default. Process-wide, as in the decomp.
void SetAnimMergingEnabled(bool enabled);
bool AnimMergingEnabled();

} // namespace Frigate
