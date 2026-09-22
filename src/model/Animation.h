#pragma once

#include <cstdint>

// Decodes and interpolates GoldenEye's per-joint animation rotation.
// Format and algorithm: docs/formats/goldeneye-animation-pose.md.
namespace Frigate {

struct JointRotation {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Reads a `width`-bit (1-16) quantized angle starting at bit `bitOffset`
// into `bitstream`, most-significant bit first, left-justified into the
// full 16-bit range (the low `16 - width` bits are zero).
uint16_t ReadAnimAngleBits(const uint8_t* bitstream, uint8_t width, uint32_t bitOffset);

// Decodes one joint's rotation from one animation frame's raw bitstream.
// `channel` is the joint's channel index (the model's joint table's mtxA,
// or mtxB when `mirror`); `angleWidthBits` is the clip's quantization
// width. When `mirror`, Y and Z are negated (X is not) to reuse one side's
// animation data for the other.
JointRotation DecodeJointRotation(const uint8_t* frameBitstream, uint8_t angleWidthBits, uint16_t channel, bool mirror);

// Shortest-path angle interpolation: moves `from` toward `to` by fraction
// `t`, wrapping through whichever of the two directions around the circle
// is shorter, rather than lerping directly (which would cross the long way
// around near the 0/2*pi boundary).
float LerpAngle(float from, float to, float t);

JointRotation LerpJointRotation(const JointRotation& from, const JointRotation& to, float t);

} // namespace Frigate
