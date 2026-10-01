#pragma once

#include "level/Matrix.h"

namespace Frigate::Level {

// One keyframe of a first-person weapon animation (Weapon1PTransformKeyframe):
// where the gun is and how it is turned, and how long the move to the next
// keyframe takes. A table ends with a keyframe flagged `final`, and its first
// and last two keyframes are the spline's guide points.
struct Keyframe {
    bool final;
    Vec3 pos;
    Vec3 rot;
    float interpParam;
    float duration;
};

// The tables the firing states play.
namespace Keyframes {
extern const Keyframe kThrowKnifeDrawBack[];
extern const Keyframe kThrowKnifeRelease[];
extern const Keyframe kGrenadeThrow[];
extern const Keyframe kTimedMineThrow[];
extern const Keyframe kProxMineThrow[];
extern const Keyframe kRemoteMineThrow[];
extern const Keyframe kFistMelee1[];
extern const Keyframe kFistMelee2[];
extern const Keyframe kSniperMelee1[];
extern const Keyframe kSniperMelee2[];
extern const Keyframe kTaserFire[];
extern const Keyframe kTaserRaise[];
extern const Keyframe kKnifeSlash1[];
extern const Keyframe kKnifeSlash2[];
} // namespace Keyframes

// gunSample1PTransform: samples `keyframes` at `time` into `matrix` (mirrored
// for the left hand). True while the animation is in progress, false once
// `time` has reached the final keyframe, which writes that pose.
bool SampleKeyframes(const Keyframe* keyframes, float time, bool leftHand, Mtxf* matrix);

} // namespace Frigate::Level
