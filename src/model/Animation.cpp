#include "model/Animation.h"

namespace Frigate {

namespace {
constexpr float kTau = 6.283185307179586f;
constexpr float kPi = 3.141592653589793f;
constexpr float kU16Max = 65536.0f;
} // namespace

uint16_t ReadAnimAngleBits(const uint8_t* bitstream, uint8_t width, uint32_t bitOffset) {
    uint32_t value = 0;
    bitstream += bitOffset / 8;
    bitOffset %= 8;
    uint8_t numBitsThisByte = 8 - static_cast<uint8_t>(bitOffset);
    uint8_t remainingBits = width;

    while (remainingBits >= numBitsThisByte) {
        remainingBits -= numBitsThisByte;
        uint32_t mask = (1u << numBitsThisByte) - 1;
        value |= (static_cast<uint32_t>(*bitstream) & mask) << remainingBits;
        value &= 0xffffu;
        bitstream++;
        numBitsThisByte = 8;
    }

    if (remainingBits > 0) {
        uint32_t mask = (1u << remainingBits) - 1;
        value |= (static_cast<uint32_t>(*bitstream) >> (numBitsThisByte - remainingBits)) & mask;
        value &= 0xffffu;
    }

    value <<= 16 - width;
    return static_cast<uint16_t>(value & 0xffffu);
}

JointRotation DecodeJointRotation(const uint8_t* frameBitstream, uint8_t angleWidthBits, uint16_t channel,
                                  bool mirror) {
    uint32_t bitOffset = static_cast<uint32_t>(channel) * angleWidthBits;
    uint16_t rawX = ReadAnimAngleBits(frameBitstream, angleWidthBits, bitOffset);
    bitOffset += angleWidthBits;
    uint16_t rawY = ReadAnimAngleBits(frameBitstream, angleWidthBits, bitOffset);
    bitOffset += angleWidthBits;
    uint16_t rawZ = ReadAnimAngleBits(frameBitstream, angleWidthBits, bitOffset);

    JointRotation rot;
    rot.x = (rawX * kTau) / kU16Max;
    if (mirror) {
        rot.y = rawY != 0 ? ((0x10000 - rawY) * kTau) / kU16Max : 0.0f;
        rot.z = rawZ != 0 ? ((0x10000 - rawZ) * kTau) / kU16Max : 0.0f;
    } else {
        rot.y = (rawY * kTau) / kU16Max;
        rot.z = (rawZ * kTau) / kU16Max;
    }
    return rot;
}

float LerpAngle(float from, float to, float t) {
    float diff = to - from;
    if (to < from) {
        diff += kTau;
    }
    if (diff < kPi) {
        from += diff * t;
        if (from >= kTau) {
            from -= kTau;
        }
    } else {
        from -= (kTau - diff) * t;
        if (from < 0) {
            from += kTau;
        }
    }
    return from;
}

JointRotation LerpJointRotation(const JointRotation& from, const JointRotation& to, float t) {
    return JointRotation{
        LerpAngle(from.x, to.x, t),
        LerpAngle(from.y, to.y, t),
        LerpAngle(from.z, to.z, t),
    };
}

JointRotation SampleJointRotation(const AnimationClip& clip, uint16_t channel, uint32_t frameA, uint32_t frameB,
                                  float frac, bool mirror) {
    if (clip.frameCount == 0) {
        return JointRotation{};
    }
    const uint32_t last = clip.frameCount - 1;
    frameA = frameA > last ? last : frameA;
    frameB = frameB > last ? last : frameB;

    JointRotation a =
        DecodeJointRotation(clip.bitstream + frameA * clip.frameSizeBytes, clip.angleWidthBits, channel, mirror);
    if (frac == 0.0f) {
        return a;
    }
    JointRotation b =
        DecodeJointRotation(clip.bitstream + frameB * clip.frameSizeBytes, clip.angleWidthBits, channel, mirror);
    return LerpJointRotation(a, b, frac);
}

} // namespace Frigate
