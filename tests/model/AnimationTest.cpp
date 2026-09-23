#include "model/Animation.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

using namespace Frigate;

namespace {

int gFailures = 0;

constexpr float kTau = 6.283185307179586f;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) < eps;
}

} // namespace

int main() {
    // Byte-aligned 8-bit reads: each byte becomes the top byte of a 16-bit
    // value (the bottom 16 - width bits are the quantization gap).
    {
        uint8_t bytes[2] = { 0xAB, 0xCD };
        Check(ReadAnimAngleBits(bytes, 8, 0) == 0xAB00, "byte-aligned 8-bit read at offset 0");
        Check(ReadAnimAngleBits(bytes, 8, 8) == 0xCD00, "byte-aligned 8-bit read at offset 8");
    }

    // Sub-byte widths split a byte into independent channels.
    {
        uint8_t bytes[1] = { 0xAB }; // 0b1010'1011
        Check(ReadAnimAngleBits(bytes, 4, 0) == 0xA000, "high nibble, left-justified to 16 bits");
        Check(ReadAnimAngleBits(bytes, 4, 4) == 0xB000, "low nibble, left-justified to 16 bits");
    }

    // A read that spans a byte boundary takes bits most-significant-first
    // across both bytes.
    {
        uint8_t bytes[2] = { 0xAB, 0xCD }; // low nibble of 0xAB, high nibble of 0xCD -> 0xBC
        Check(ReadAnimAngleBits(bytes, 8, 4) == 0xBC00, "8-bit read spanning a byte boundary");
    }

    // A joint's rotation is three consecutive width-bit angles (X, Y, Z)
    // starting at channel * width bits into the frame.
    {
        uint8_t frame[3] = { 0x20, 0x40, 0x60 };
        JointRotation rot = DecodeJointRotation(frame, 8, 0, /*mirror=*/false);
        Check(Near(rot.x, kTau * 0x2000 / 65536.0f), "unmirrored joint X");
        Check(Near(rot.y, kTau * 0x4000 / 65536.0f), "unmirrored joint Y");
        Check(Near(rot.z, kTau * 0x6000 / 65536.0f), "unmirrored joint Z");
    }

    // Mirroring negates Y and Z (wrapping through the full circle) but
    // leaves X untouched, matching a left/right joint pair sharing one
    // side's animation data.
    {
        uint8_t frame[3] = { 0x20, 0x40, 0x60 };
        JointRotation unmirrored = DecodeJointRotation(frame, 8, 0, /*mirror=*/false);
        JointRotation mirrored = DecodeJointRotation(frame, 8, 0, /*mirror=*/true);
        Check(Near(mirrored.x, unmirrored.x), "mirroring leaves X untouched");
        Check(Near(mirrored.y, kTau - unmirrored.y), "mirroring negates Y through the full circle");
        Check(Near(mirrored.z, kTau - unmirrored.z), "mirroring negates Z through the full circle");
    }

    // A raw angle of exactly zero stays zero when mirrored rather than
    // wrapping to a full 2*pi turn.
    {
        uint8_t frame[3] = { 0x00, 0x00, 0x00 };
        JointRotation rot = DecodeJointRotation(frame, 8, 0, /*mirror=*/true);
        Check(Near(rot.x, 0.0f) && Near(rot.y, 0.0f) && Near(rot.z, 0.0f), "zero angle mirrors to zero, not 2*pi");
    }

    // A joint's channel index gives its bit offset directly (not
    // multiplied by 3), matching the decomp's `mtxA * width`.
    {
        uint8_t frame[4] = { 0, 0x20, 0x40, 0x60 };
        JointRotation rot = DecodeJointRotation(frame, 8, 1, /*mirror=*/false);
        Check(Near(rot.x, kTau * 0x2000 / 65536.0f), "channel index is the bit offset, not a joint counter");
    }

    // Ordinary interpolation, no wraparound.
    {
        float result = LerpAngle(0.0f, kTau / 4.0f, 0.5f);
        Check(Near(result, kTau / 8.0f), "halfway lerp with no wraparound");
    }

    // Interpolating between two angles near the 0/2*pi boundary takes the
    // short way around through zero, not the long way through pi.
    {
        float result = LerpAngle(0.1f, kTau - 0.1f, 0.5f);
        Check(result < 0.5f || result > kTau - 0.5f, "wraparound lerp takes the shorter path through zero");
    }

    // Sampling blends frameA toward frameB by the fraction; a fraction of 0 is
    // frameA alone, and frames past the clip's end are clamped to its last.
    {
        // Two frames of one joint, 16-bit angles: Z = 0, then Z = 0x4000 (pi/2).
        uint8_t frames[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x40, 0x00 };
        AnimationClip clip;
        clip.angleWidthBits = 16;
        clip.frameSizeBytes = 6;
        clip.bitstream = frames;
        clip.frameCount = 2;

        Check(Near(SampleJointRotation(clip, 0, 0, 1, 0.5f, false).z, kTau / 8.0f),
              "halfway between two frames is halfway between their angles");
        Check(Near(SampleJointRotation(clip, 0, 1, 0, 0.0f, false).z, kTau / 4.0f), "a zero fraction is frameA alone");
        Check(Near(SampleJointRotation(clip, 0, 7, 7, 0.5f, false).z, kTau / 4.0f),
              "frames past the last are clamped to it");

        AnimationClip empty;
        JointRotation rest = SampleJointRotation(empty, 0, 0, 0, 0.5f, false);
        Check(rest.x == 0.0f && rest.y == 0.0f && rest.z == 0.0f, "a clip with no frames is rest pose");
    }

    if (gFailures == 0) {
        std::printf("AnimationTest: all checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
