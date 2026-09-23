#include "model/RootMotion.h"

namespace Frigate {

namespace {
constexpr float kTau = 6.2831855f;
constexpr float kU16Max = 65536.0f;
} // namespace

uint16_t ReadRootMotionValue(const AnimationClip& clip, uint32_t field, uint32_t extraBitOffset) {
    uint32_t result = 0;
    const RootMotionDescriptor& desc = clip.rootDescriptors[field];
    uint8_t bitsRemaining = desc.bitCount;

    if (bitsRemaining > 0) {
        uint32_t totalBitOffset = extraBitOffset + desc.bitOffset;
        const uint8_t* byte = clip.rootBitstream + (totalBitOffset >> 3);
        totalBitOffset &= 7;
        uint8_t bitsThisRead = 8 - totalBitOffset;

        if (bitsRemaining >= bitsThisRead) {
            do {
                uint32_t mask = (1u << bitsThisRead) - 1;
                bitsRemaining -= bitsThisRead;
                result |= (static_cast<uint32_t>(*byte) & mask) << bitsRemaining;
                result &= 0xffffu;
                byte++;
                bitsThisRead = 8;
            } while (bitsRemaining >= 8);
        }

        if (bitsRemaining > 0) {
            result |= (static_cast<uint32_t>(*byte) >> (bitsThisRead - bitsRemaining)) & ((1u << bitsRemaining) - 1);
            result &= 0xffffu;
        }

        bitsRemaining = desc.bitCount;

        if (bitsRemaining < 16) {
            uint32_t signBit = 1u << (bitsRemaining - 1);
            if (result & signBit) {
                result = (result | (((1u << (16 - bitsRemaining)) - 1) << bitsRemaining)) & 0xffffu;
            }
        }
    }

    return static_cast<uint16_t>(desc.valueOffset + result);
}

RootMotionSample SampleRootMotion(const AnimationClip& clip, const std::vector<JointChannels>& joints,
                                  uint16_t rootJoint, bool mirror, int32_t frame) {
    const uint32_t frameBits = static_cast<uint32_t>(clip.rootStrideBits) * static_cast<uint32_t>(frame);
    const uint32_t base = mirror ? joints[rootJoint].mtxB : joints[rootJoint].mtxA;

    // Components narrow to s16, as the decomp's coord16 does.
    int16_t x = static_cast<int16_t>(ReadRootMotionValue(clip, base, frameBits));
    const int16_t y = static_cast<int16_t>(ReadRootMotionValue(clip, base + 1, frameBits));
    const int16_t z = static_cast<int16_t>(ReadRootMotionValue(clip, base + 2, frameBits));
    uint32_t angleRaw = ReadRootMotionValue(clip, base + 3, frameBits);

    if (mirror) {
        x = static_cast<int16_t>(-x);
        if (angleRaw != 0) {
            angleRaw = 0x10000u - angleRaw;
        }
    }

    RootMotionSample sample;
    sample.pos[0] = static_cast<float>(x);
    sample.pos[1] = static_cast<float>(y);
    sample.pos[2] = static_cast<float>(z);
    sample.angle = (static_cast<float>(static_cast<uint16_t>(angleRaw)) * kTau) / kU16Max;
    return sample;
}

} // namespace Frigate
