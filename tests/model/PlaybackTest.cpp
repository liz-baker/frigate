#include "model/Playback.h"
#include "model/RootMotion.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

using namespace Frigate;

namespace {

int gFailures = 0;

constexpr float kTau = 6.2831855f;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) < eps;
}

// A clip whose root joint moves in X by one byte per frame (sign-extended
// from 8 bits), stands at a constant Y of 100, and never turns.
struct TestClip {
    std::vector<RootMotionDescriptor> descriptors = {
        { 0, 8, 0 },   // X
        { 0, 0, 100 }, // Y: constant
        { 0, 0, 0 },   // Z: constant
        { 0, 0, 0 },   // heading: constant
    };
    std::vector<uint8_t> bytes;
    AnimationClip clip;

    TestClip(std::vector<uint8_t> xPerFrame, bool loop) : bytes(std::move(xPerFrame)) {
        clip.frameCount = static_cast<uint32_t>(bytes.size());
        clip.loop = loop;
        clip.rootStrideBits = 8;
        clip.rootDescriptors = descriptors.data();
        clip.rootDescriptorCount = static_cast<uint32_t>(descriptors.size());
        clip.rootBitstream = bytes.data();
    }
};

const std::vector<JointChannels> kJoints = { { 0, 4 } };

ModelPlayback MakePlayback() {
    ModelPlayback playback;
    playback.hasRoot = true;
    playback.joints = &kJoints;
    playback.rootJoint = 0;
    return playback;
}

void TestReadRootMotionValue() {
    RootMotionDescriptor desc{ 3, 5, 0 };
    AnimationClip clip;
    clip.rootDescriptors = &desc;
    clip.rootDescriptorCount = 1;

    // The 5 bits at offset 3 of 0b101'10101 are 0b10101: sign bit set, so -11.
    const uint8_t signBits[1] = { 0b10110101 };
    clip.rootBitstream = signBits;
    Check(static_cast<int16_t>(ReadRootMotionValue(clip, 0, 0)) == -11, "a 5-bit field sign-extends");

    // 8 bits at offset 6 of 0b000000'11 0b11'000000 are 0b11110000: -16.
    const uint8_t spanning[2] = { 0b00000011, 0b11000000 };
    desc = { 6, 8, 0 };
    clip.rootBitstream = spanning;
    Check(static_cast<int16_t>(ReadRootMotionValue(clip, 0, 0)) == -16, "a field spanning bytes reads across them");

    // extraBitOffset shifts the read, as the per-frame stride does.
    const uint8_t frames[2] = { 5, 200 };
    desc = { 0, 8, 0 };
    clip.rootBitstream = frames;
    Check(static_cast<int16_t>(ReadRootMotionValue(clip, 0, 8)) == -56,
          "the frame offset is added to the field's bit offset");

    // A bit count of 0 is just the value offset; otherwise it's added to the field.
    desc = { 0, 0, 7 };
    Check(ReadRootMotionValue(clip, 0, 0) == 7, "a zero bit count is just the value offset");
    desc = { 0, 8, 10 };
    Check(ReadRootMotionValue(clip, 0, 0) == 15, "the value offset is added to the decoded field");
}

void TestSampleRootMotionMirror() {
    // One frame, 24 bits: X (8 bits), then a 16-bit heading of 0x4000. Joint 0's
    // mtxA channels are descriptors 0-3, its mtxB channels 4-7.
    const uint8_t bits[3] = { 10, 0x40, 0x00 };
    const std::vector<RootMotionDescriptor> descriptors = {
        { 0, 8, 0 }, { 0, 0, 100 }, { 0, 0, 0 }, { 8, 16, 0 }, { 0, 8, 0 }, { 0, 0, 100 }, { 0, 0, 0 }, { 8, 16, 0 },
    };
    AnimationClip clip;
    clip.frameCount = 1;
    clip.rootStrideBits = 24;
    clip.rootDescriptors = descriptors.data();
    clip.rootDescriptorCount = static_cast<uint32_t>(descriptors.size());
    clip.rootBitstream = bits;

    RootMotionSample plain = SampleRootMotion(clip, kJoints, 0, false, 0);
    Check(Near(plain.pos[0], 10.0f) && Near(plain.pos[1], 100.0f) && Near(plain.pos[2], 0.0f),
          "an unmirrored root reads its mtxA channel's position");
    Check(Near(plain.angle, kTau * 0x4000 / 65536.0f), "the heading scales its raw 16 bits to radians");

    RootMotionSample mirrored = SampleRootMotion(clip, kJoints, 0, true, 0);
    Check(Near(mirrored.pos[0], -10.0f), "mirroring negates X");
    Check(Near(mirrored.pos[1], 100.0f), "mirroring leaves Y alone");
    Check(Near(mirrored.angle, kTau * (0x10000 - 0x4000) / 65536.0f),
          "mirroring negates the heading through the circle");
}

void TestSetAnimFrame() {
    TestClip test({ 0, 10, 20, 30 }, true);
    ModelPlayback playback = MakePlayback();
    playback.anim = &test.clip;

    playback.SetAnimFrame(1.25f);
    Check(playback.frameA == 1 && playback.frameB == 2 && Near(playback.frameFrac, 0.25f) &&
              Near(playback.frame, 1.25f),
          "a forward frame splits into two frames and a fraction");

    playback.SetAnimFrame(3.5f);
    Check(playback.frameA == 3 && playback.frameB == 0, "a looping clip wraps frameB past the end");

    // Backwards, frameB is the earlier frame and the fraction is negated,
    // as in the decomp.
    playback.speed = -1.0f;
    playback.SetAnimFrame(2.75f);
    Check(playback.frameA == 2 && playback.frameB == 1 && Near(playback.frameFrac, -0.75f) &&
              Near(playback.frame, 2.75f),
          "playing backwards blends toward the earlier frame");

    TestClip held({ 0, 10, 20, 30 }, false);
    playback.speed = 1.0f;
    playback.anim = &held.clip;
    playback.SetAnimFrame(3.5f);
    Check(playback.frameA == 3 && playback.frameB == 3 && playback.frameFrac == 0.0f && playback.frame == 3.0f,
          "a non-looping clip holds its last frame");

    playback.endFrame = 1.0f;
    playback.SetAnimFrame(2.0f);
    Check(playback.frameA == 1 && playback.frameB == 1, "an end frame clamps the frames");
}

void TestSetAnimationStartsRootMotion() {
    TestClip test({ 0, 10, 20, 30 }, false);
    ModelPlayback playback = MakePlayback();

    playback.SetAnimation(&test.clip, false, 0.0f, 1.0f, 0.0f);

    Check(playback.anim == &test.clip && playback.anim2 == nullptr,
          "SetAnimation with no merge time leaves no secondary clip");
    Check(playback.root.hasTarget, "SetAnimation marks the root's target valid");
    Check(Near(playback.root.startPos.x, 0.0f) && Near(playback.root.targetPos.x, 10.0f) &&
              Near(playback.root.targetPos.y, 100.0f),
          "the target is one frame's root motion ahead of the start");

    // The instance's scale multiplies the clip's translation.
    ModelPlayback scaled = MakePlayback();
    scaled.scale = 0.5f;
    scaled.SetAnimation(&test.clip, false, 0.0f, 1.0f, 0.0f);
    Check(Near(scaled.root.targetPos.x, 5.0f) && Near(scaled.root.targetPos.y, 50.0f),
          "scale multiplies the root motion");

    // Root motion is rotated by the model's heading.
    ModelPlayback turned = MakePlayback();
    turned.root.heading = kTau / 4.0f;
    turned.SetAnimation(&test.clip, false, 0.0f, 1.0f, 0.0f);
    Check(Near(turned.root.targetPos.x, 0.0f, 0.01f) && Near(turned.root.targetPos.z, -10.0f, 0.01f),
          "root motion is rotated by the current heading");
}

void TestSetAnimationMergesFromThePreviousClip() {
    TestClip first({ 0, 10, 20, 30 }, false);
    TestClip second({ 0, 4, 8, 12 }, false);
    ModelPlayback playback = MakePlayback();

    playback.SetAnimation(&first.clip, false, 1.0f, 1.0f, 0.0f);
    playback.SetAnimation(&second.clip, false, 0.0f, 1.0f, 8.0f);

    Check(playback.anim2 == &first.clip && Near(playback.frame2, 1.0f), "the previous clip becomes the secondary clip");
    Check(Near(playback.mergeWeight, 1.0f) && Near(playback.mergeDuration, 8.0f),
          "a merge starts at full secondary weight");
    Check(playback.root.hasMergeSource, "the root records the merged-from clip's motion");

    playback.SetAnimation(&first.clip, false, 0.0f, 1.0f, 0.0f);
    Check(playback.anim2 == nullptr && playback.mergeWeight == 0.0f && playback.mergeDuration == 0.0f,
          "a zero merge time drops the secondary clip");
}

void TestTickAnimAdvancesByPlaySpeedAndSpeed() {
    TestClip test({ 0, 10, 20, 30, 40, 50 }, false);
    ModelPlayback playback = MakePlayback();
    playback.SetAnimation(&test.clip, false, 0.0f, 2.0f, 0.0f);
    playback.SetAnimPlaySpeed(0.5f, 0.0f);

    playback.TickAnim(1, false);
    Check(Near(playback.frame, 1.0f), "one tick advances by play speed * speed");

    playback.TickAnim(2, false);
    Check(Near(playback.frame, 3.0f), "ticks accumulate");
}

void TestTickAnimRampsSpeed() {
    TestClip test({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 }, false);
    ModelPlayback playback = MakePlayback();
    playback.SetAnimation(&test.clip, false, 0.0f, 1.0f, 0.0f);
    playback.SetAnimSpeed(3.0f, 4.0f);

    Check(Near(playback.speed, 1.0f) && Near(playback.newSpeed, 3.0f), "a speed ramp keeps the old speed until ticked");

    playback.TickAnim(2, false);
    Check(Near(playback.speed, 2.0f), "halfway through the ramp the speed is halfway to the new one");
    playback.TickAnim(2, false);
    Check(Near(playback.speed, 3.0f) && playback.timeSpeed == 0.0f, "the ramp ends at the new speed");
}

void TestTickAnimLoopsToLoopFrame() {
    TestClip test({ 0, 1, 2, 3, 4, 5 }, true);
    ModelPlayback playback = MakePlayback();
    playback.SetAnimation(&test.clip, false, 3.0f, 1.0f, 0.0f);
    playback.SetAnimLooping(1.0f, 0.0f);

    int flips = 0;
    playback.animFlipFunc = [&flips] { flips++; };

    playback.TickAnim(2, false); // 3 -> 5, the last frame and the loop limit
    Check(flips == 1, "reaching the last frame flips the loop");
    Check(Near(playback.frame, 1.0f) && playback.animLooping, "the loop restarts at the loop frame");
}

void TestTickAnimFadesOutTheMergedClip() {
    TestClip first({ 0, 10, 20, 30, 40, 50 }, false);
    TestClip second({ 0, 4, 8, 12, 16, 20 }, false);
    ModelPlayback playback = MakePlayback();
    playback.SetAnimation(&first.clip, false, 0.0f, 1.0f, 0.0f);
    playback.SetAnimation(&second.clip, false, 0.0f, 1.0f, 0.0f);
    playback.SetAnimation(&first.clip, false, 0.0f, 1.0f, 4.0f);
    Check(playback.anim2 == &second.clip, "the second clip is the merge source");

    playback.TickAnim(1, false);
    Check(Near(playback.mergeWeight, 0.75f), "the merge weight falls linearly over the merge time");
    playback.TickAnim(3, false);
    Check(playback.anim2 == nullptr && playback.mergeWeight == 0.0f,
          "the merged clip is dropped when the merge time ends");
}

void TestTickAnimWithRootMovesTheTarget() {
    TestClip test({ 0, 10, 20, 30, 40, 50 }, false);
    ModelPlayback playback = MakePlayback();
    playback.SetAnimation(&test.clip, false, 0.0f, 1.0f, 0.0f);

    playback.TickAnim(1, true);
    Check(Near(playback.frame, 1.0f) && playback.frameA == 1 && playback.frameB == 2,
          "ticking with root motion lands on the next frame");
    // Each frame's root motion is a step from the previous one: the old target
    // becomes the start, and the next frame's step is added to it.
    Check(Near(playback.root.startPos.x, 10.0f) && Near(playback.root.targetPos.x, 30.0f),
          "the root's start and target advance one frame of motion");
}

void TestCalcPosInterpolatesAndAdvances() {
    TestClip test({ 0, 10, 20, 30, 40, 50 }, false);

    ModelPlayback playback = MakePlayback();
    playback.SetAnimation(&test.clip, false, 0.0f, 1.0f, 0.0f);
    playback.TickAnim(1, true);
    playback.SetAnimFrame2(1.5f, 0.0f);

    playback.CalcPos();
    Check(Near(playback.root.pos.x, 20.0f) && Near(playback.root.pos.y, 100.0f),
          "CalcPos places the model between the step's start (10) and target (30)");
    Check(Near(playback.root.startPos.x, 10.0f) && Near(playback.root.targetPos.x, 30.0f),
          "CalcPos leaves the step in place when nothing moved the model off the interpolated position");

    // A move filter can veto or adjust the move.
    ModelPlayback vetoed = MakePlayback();
    vetoed.SetAnimation(&test.clip, false, 0.0f, 1.0f, 0.0f);
    vetoed.TickAnim(1, true);
    vetoed.SetAnimFrame2(1.5f, 0.0f);
    vetoed.moveFilter = [](const Vec3&, Vec3&, float&) { return false; };
    vetoed.CalcPos();
    Check(vetoed.root.pos.x == 0.0f, "a move filter returning false leaves the position alone");

    ModelPlayback adjusted = MakePlayback();
    adjusted.SetAnimation(&test.clip, false, 0.0f, 1.0f, 0.0f);
    adjusted.TickAnim(1, true);
    adjusted.SetAnimFrame2(1.5f, 0.0f);
    adjusted.moveFilter = [](const Vec3&, Vec3& to, float& ground) {
        to.x = 12.0f;
        ground = 5.0f;
        return true;
    };
    adjusted.CalcPos();
    Check(Near(adjusted.root.pos.x, 12.0f) && Near(adjusted.root.pos.y, 105.0f),
          "a move filter's destination and ground height are used");
    Check(Near(adjusted.root.startPos.x, 2.0f),
          "the step shifts by however far the filter placed the model from the interpolated position");
}

void TestSetPositionAndHeadingShiftTheRootState() {
    TestClip test({ 0, 10, 20 }, false);
    ModelPlayback playback = MakePlayback();
    playback.SetAnimation(&test.clip, false, 0.0f, 1.0f, 0.0f);
    playback.SetPosition({ 5.0f, 0.0f, 7.0f });
    Check(Near(playback.root.pos.x, 5.0f) && Near(playback.root.startPos.x, 5.0f) &&
              Near(playback.root.targetPos.x, 15.0f) && Near(playback.root.startPos.z, 7.0f),
          "SetPosition moves the step's start and target with the model");

    playback.SetHeading(1.0f);
    Check(Near(playback.root.heading, 1.0f) && Near(playback.root.startHeading, 1.0f),
          "SetHeading turns the model and its step");
}

void TestMakePoseAndPlacement() {
    TestClip first({ 0, 10, 20, 30 }, false);
    TestClip second({ 0, 4, 8, 12 }, false);
    ModelPlayback playback = MakePlayback();
    playback.scale = 0.5f;
    playback.SetAnimation(&first.clip, false, 1.25f, 1.0f, 0.0f);

    AnimationPose pose = MakePose(playback);
    Check(pose.clip == &first.clip && pose.joints == &kJoints && pose.frameA == 1 && pose.frameB == 2 &&
              Near(pose.frameFrac, 0.25f),
          "the pose carries the primary clip and its frames");
    Check(pose.secondaryClip == nullptr, "no merge, no secondary clip");

    playback.SetAnimation(&second.clip, true, 0.0f, 1.0f, 8.0f);
    pose = MakePose(playback);
    Check(pose.secondaryClip == &first.clip && pose.mirror && !pose.secondaryMirror && Near(pose.blend, 1.0f) &&
              pose.secondaryFrameA == 1,
          "a merge adds the previous clip with its own mirror flag and the merge weight");

    playback.mergeWeight = 0.0f;
    Check(MakePose(playback).secondaryClip == nullptr, "a zero merge weight drops the secondary clip");

    playback.root.pos = { 1.0f, 2.0f, 3.0f };
    playback.root.heading = 0.5f;
    RootPlacement placement = MakePlacement(playback);
    Check(placement.position[0] == 1.0f && placement.position[1] == 2.0f && placement.position[2] == 3.0f &&
              placement.heading == 0.5f && placement.scale == 0.5f,
          "the placement is the root's position and heading and the model's scale");

    playback.root.headingOverrideBlend = 0.5f;
    playback.root.headingOverride = 1.5f;
    Check(Near(MakePlacement(playback).heading, 1.0f), "a heading override blends in by its weight");
}

void TestNoRootNodeMeansNoRootMotion() {
    TestClip test({ 0, 10, 20 }, false);
    ModelPlayback playback;
    playback.joints = &kJoints;
    playback.SetAnimation(&test.clip, false, 0.0f, 1.0f, 0.0f);
    playback.TickAnim(1, true);
    playback.CalcPos();
    Check(playback.root.pos.x == 0.0f && Near(playback.frame, 1.0f),
          "a model with no header root plays without root motion");
}

} // namespace

int main() {
    TestReadRootMotionValue();
    TestSampleRootMotionMirror();
    TestSetAnimFrame();
    TestSetAnimationStartsRootMotion();
    TestSetAnimationMergesFromThePreviousClip();
    TestTickAnimAdvancesByPlaySpeedAndSpeed();
    TestTickAnimRampsSpeed();
    TestTickAnimLoopsToLoopFrame();
    TestTickAnimFadesOutTheMergedClip();
    TestTickAnimWithRootMovesTheTarget();
    TestCalcPosInterpolatesAndAdvances();
    TestSetPositionAndHeadingShiftTheRootState();
    TestNoRootNodeMeansNoRootMotion();
    TestMakePoseAndPlacement();

    if (gFailures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("All playback tests passed\n");
    return 0;
}
