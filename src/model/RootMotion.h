#pragma once

#include <cstdint>
#include <vector>

#include "model/Animation.h"
#include "model/ModelWalk.h"

// Decodes a character clip's root motion: the position and heading change the
// clip applies to the model as a whole, as opposed to its per-joint rotations.
// Format: docs/formats/goldeneye-animation-clip-resource.md.
namespace Frigate {

// Reads root-motion field `field` (an index into `clip`'s descriptors) at
// `extraBitOffset` bits into the root-motion bitstream: the descriptor's bit
// count of bits, sign-extended from that width, plus its value offset. A
// descriptor with a bit count of 0 is just the value offset.
uint16_t ReadRootMotionValue(const AnimationClip& clip, uint32_t field, uint32_t extraBitOffset);

struct RootMotionSample {
    // Position components as the clip stores them, in model units before the
    // instance's scale.
    float pos[3] = { 0.0f, 0.0f, 0.0f };
    // Heading change, radians in [0, 2*pi).
    float angle = 0.0f;
};

// Decodes the root joint's position and heading at integer `frame`. `joints`
// is the model's joint table, `rootJoint` the root header node's joint (the
// decomp's AnimPart); `mirror` selects the joint's mtxB channel and negates X
// and the heading.
RootMotionSample SampleRootMotion(const AnimationClip& clip, const std::vector<JointChannels>& joints,
                                  uint16_t rootJoint, bool mirror, int32_t frame);

} // namespace Frigate
