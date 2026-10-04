#include "level/GunKeyframes.h"

#include <bit>
#include <cstdint>

#include "model/Quaternion.h"

namespace Frigate::Level {

Vec3 CubicSpline(const Vec3& prev, const Vec3& start, const Vec3& end, const Vec3& next, float fraction,
                 float tangentScale) {
    const float square = fraction * fraction;
    const float scale = tangentScale;
    const float cube = square * fraction;

    const float a = ((2.0f * square) - (fraction + cube)) * scale;
    const float b = (((2.0f - scale) * cube) + (square * (scale - 3.0f))) + 1.0f;
    const float c = (((scale - 2.0f) * cube) + (square * (3.0f - (2.0f * scale)))) + (fraction * scale);
    Vec3 out{};
    for (size_t i = 0; i < 3; i++) {
        out[i] = (((a * prev[i]) + (b * start[i])) + (c * end[i])) + (((cube - square) * tangentScale) * next[i]);
    }
    return out;
}

namespace {

// A float written as its IEEE bits, the way the decomp's two knife tables are.
constexpr float Word(uint32_t bits) {
    return std::bit_cast<float>(bits);
}

Quaternion Euler(const Vec3& rot) {
    return EulerToQuaternion({ rot[0], rot[1], rot[2] });
}

} // namespace

namespace Keyframes {

const Keyframe kThrowKnifeDrawBack[] = {
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.0f, 0.0f, 4.5f }, { 5.576369f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.0f, 0.0f, 20.5f }, { 5.26209f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.0f, 3.0f, 5.5f }, { 0.031375f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { true, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f },
};

const Keyframe kThrowKnifeRelease[] = {
    { false, { 0.0f, 0.0f, 4.5f }, { 5.576369f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.0f, 0.0f, 20.5f }, { 5.26209f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.0f, 3.0f, 5.5f }, { 0.031375f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.0f, -20.0f, 18.0f }, { 0.785458f, 0.0f, 0.0f }, 0.5f, 20.0f },
    { false, { 0.0f, -20.0f, 18.0f }, { 0.785458f, 0.0f, 0.0f }, 0.5f, 20.0f },
    { true, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f },
};

const Keyframe kGrenadeThrow[] = {
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 4.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 4.0f },
    { false, { 10.0f, 12.5f, 17.5f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 4.0f },
    { false, { 10.0f, 34.5f, 25.5f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 10.0f },
    { false, { 10.0f, 34.5f, 25.5f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 10.0f },
    { true, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f },
};

const Keyframe kTimedMineThrow[] = {
    { false, { 10.0f, 34.5f, 25.5f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 10.0f },
    { false, { 10.0f, 34.5f, 25.5f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 10.0f },
    { false, { 10.0f, 12.5f, 17.5f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 10.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 10.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 10.0f },
    { true, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f },
};

const Keyframe kProxMineThrow[] = {
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 4.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 4.0f },
    { false, { 0.0f, 0.0f, 4.5f }, { 5.576369f, 0.0f, 0.0f }, 0.5f, 4.0f },
    { false, { 0.0f, 0.0f, 20.5f }, { 5.26209f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.0f, 3.0f, 5.5f }, { 0.031375f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { true, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f },
};

const Keyframe kRemoteMineThrow[] = {
    { false, { 0.0f, 0.0f, 4.5f }, { 5.576369f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.0f, 0.0f, 20.5f }, { 5.26209f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.0f, 3.0f, 5.5f }, { 0.031375f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.0f, -20.0f, 18.0f }, { 0.785458f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 20.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 20.0f },
    { true, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f },
};

const Keyframe kFistMelee1[] = {
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 10.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 10.0f },
    { false, { 6.0f, 23.0f, 0.0f }, { 5.91572f, 0.085832f, 0.219482f }, 0.5f, 10.0f },
    { false, { 18.0f, 35.0f, 9.5f }, { 4.998193f, 0.084203f, 0.268954f }, 0.5f, 10.0f },
    { false, { -20.0f, 25.5f, 4.0f }, { 0.126148f, 0.304284f, 0.548047f }, 0.5f, 10.0f },
    { false, { -28.0f, -4.0f, 2.0f }, { 0.506821f, 0.51473f, 0.484098f }, 0.5f, 1.0f },
    { false, { -28.0f, -4.0f, 2.0f }, { 0.506821f, 0.51473f, 0.484098f }, 0.5f, 1.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 20.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 20.0f },
    { true, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f },
};

const Keyframe kFistMelee2[] = {
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 10.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 10.0f },
    { false, { -6.0f, 23.0f, 0.0f }, { 5.08683f, 6.131295f, 5.534376f }, 0.5f, 10.0f },
    { false, { -18.0f, 35.0f, 9.5f }, { 4.880698f, 0.070396f, 5.53615f }, 0.5f, 10.0f },
    { false, { 8.0f, 25.5f, 4.0f }, { 0.107213f, 6.062361f, 5.404225f }, 0.5f, 10.0f },
    { false, { 28.0f, -4.0f, 2.0f }, { 0.107213f, 6.062361f, 5.404225f }, 0.5f, 1.0f },
    { false, { 28.0f, -4.0f, 2.0f }, { 0.107213f, 6.062361f, 5.404225f }, 0.5f, 1.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 20.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 20.0f },
    { true, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f },
};

const Keyframe kSniperMelee1[] = {
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 9.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 9.5f, -0.5f, 3.5f }, { 0.291053f, 5.584375f, 6.212358f }, 0.5f, 8.0f },
    { false, { 18.0f, 7.5f, 3.5f }, { 0.439372f, 5.945201f, 5.993666f }, 0.5f, 8.0f },
    { false, { -9.0f, 8.5f, 5.5f }, { 0.704803f, 0.194459f, 6.168447f }, 0.5f, 7.0f },
    { false, { -29.0f, -5.5f, 5.5f }, { 2.281831f, 1.106353f, 1.489998f }, 0.5f, 7.0f },
    { false, { -57.5f, -27.5f, 5.5f }, { 2.281831f, 1.106353f, 1.489998f }, 0.5f, 7.0f },
    { false, { -19.5f, -20.0f, 5.5f }, { 1.22519f, 0.726087f, 1.210713f }, 0.5f, 15.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 20.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 20.0f },
    { true, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f },
};

const Keyframe kSniperMelee2[] = {
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 9.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { -15.5f, 0.5f, 15.0f }, { 0.9344959855079651f, 0.6256099939346313f, 0.2237969934940338f }, 0.5f, 8.0f },
    { false, { -23.0f, 2.0f, 12.0f }, { 1.8016400337219238f, 0.9494050145149231f, 0.6307389736175537f }, 0.5f, 8.0f },
    { false, { -18.0f, -0.5f, 4.0f }, { 0.8478249907493591f, 0.9247649908065796f, 0.07744300365447998f }, 0.5f, 7.0f },
    { false, { 10.5f, 5.0f, 2.5f }, { 0.22940599918365479f, 0.24570399522781372f, 0.09906300157308578f }, 0.5f, 7.0f },
    { false, { 18.0f, 5.0f, 2.5f }, { 0.03281300142407417f, 6.20933723449707f, 0.1350640058517456f }, 0.5f, 7.0f },
    { false, { 9.5f, 3.5f, -1.5f }, { 6.273238182067871f, 6.005795001983643f, 0.08971499651670456f }, 0.5f, 7.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 20.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 20.0f },
    { true, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f },
};

const Keyframe kTaserFire[] = {
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.5f, -6.0f, -8.0f }, { 0.439468f, 0.278829f, 0.195178f }, 0.5f, 8.0f },
    { false, { -2.0f, -8.0f, -10.0f }, { 1.101655f, 0.460753f, 0.570961f }, 0.5f, 8.0f },
    { false, { -2.0f, -8.0f, -10.0f }, { 1.101655f, 0.460753f, 0.570961f }, 0.5f, 8.0f },
    { true, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f },
};

const Keyframe kTaserRaise[] = {
    { false, { -2.0f, -8.0f, -10.0f }, { 1.101655f, 0.460753f, 0.570961f }, 0.5f, 8.0f },
    { false, { -2.0f, -8.0f, -10.0f }, { 1.101655f, 0.460753f, 0.570961f }, 0.5f, 8.0f },
    { false, { 0.5f, -6.0f, -8.0f }, { 0.439468f, 0.278829f, 0.195178f }, 0.5f, 8.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.5f, 8.0f },
    { true, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f },
};

const Keyframe kKnifeSlash1[] = {
    { false,
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      Word(0x3f000000u),
      Word(0x41000000u) },
    { false,
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      Word(0x3f000000u),
      Word(0x41000000u) },
    { false,
      { Word(0x40c00000u), Word(0xbfc00000u), Word(0x00000000u) },
      { Word(0x40b487b1u), Word(0x3e70c0adu), Word(0x3e0ae536u) },
      Word(0x3f000000u),
      Word(0x41000000u) },
    { false,
      { Word(0x41480000u), Word(0xc0600000u), Word(0x00000000u) },
      { Word(0x40c159ecu), Word(0x3d374bc7u), Word(0x3f0e4378u) },
      Word(0x3f000000u),
      Word(0x41000000u) },
    { false,
      { Word(0xc1200000u), Word(0xc1300000u), Word(0x00000000u) },
      { Word(0x3f9ed962u), Word(0x3ea24c40u), Word(0x3f8b0df1u) },
      Word(0x3f000000u),
      Word(0x41000000u) },
    { false,
      { Word(0xc1600000u), Word(0xc1700000u), Word(0x00000000u) },
      { Word(0x3fea4780u), Word(0x40c498e3u), Word(0x3fa316d3u) },
      Word(0x3f000000u),
      Word(0x41200000u) },
    { false,
      { Word(0xbf800000u), Word(0xc1100000u), Word(0x00000000u) },
      { Word(0x3ec4bba1u), Word(0x3eb87c42u), Word(0x3dd75968u) },
      Word(0x3f000000u),
      Word(0x41200000u) },
    { false,
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      Word(0x3f000000u),
      Word(0x41a00000u) },
    { false,
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      Word(0x3f000000u),
      Word(0x41a00000u) },
    { true,
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      Word(0x00000000u),
      Word(0x00000000u) },
};

const Keyframe kKnifeSlash2[] = {
    { false,
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      Word(0x3f000000u),
      Word(0x41000000u) },
    { false,
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      Word(0x3f000000u),
      Word(0x41000000u) },
    { false,
      { Word(0xc1080000u), Word(0xc0c00000u), Word(0x00000000u) },
      { Word(0x40af7506u), Word(0x40bab4b9u), Word(0x40c2a5c2u) },
      Word(0x3f000000u),
      Word(0x41000000u) },
    { false,
      { Word(0xc0400000u), Word(0xc0600000u), Word(0x00000000u) },
      { Word(0x3ece08f2u), Word(0x40b75721u), Word(0x40b62409u) },
      Word(0x3f000000u),
      Word(0x41000000u) },
    { false,
      { Word(0xbf000000u), Word(0xc1080000u), Word(0x00000000u) },
      { Word(0x3f9dfd7au), Word(0x40b768cdu), Word(0x40b37bdfu) },
      Word(0x3f000000u),
      Word(0x41000000u) },
    { false,
      { Word(0x40e00000u), Word(0xc1e40000u), Word(0xbfc00000u) },
      { Word(0x3fa74949u), Word(0x40b63ebcu), Word(0x40b6443du) },
      Word(0x3f000000u),
      Word(0x41200000u) },
    { false,
      { Word(0xbfc00000u), Word(0xc1100000u), Word(0x00000000u) },
      { Word(0x3d8adeecu), Word(0x40c84e72u), Word(0x3e506749u) },
      Word(0x3f000000u),
      Word(0x41200000u) },
    { false,
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      Word(0x3f000000u),
      Word(0x41a00000u) },
    { false,
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      Word(0x3f000000u),
      Word(0x41a00000u) },
    { true,
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      { Word(0x00000000u), Word(0x00000000u), Word(0x00000000u) },
      Word(0x00000000u),
      Word(0x00000000u) },
};

} // namespace Keyframes

bool SampleKeyframes(const Keyframe* keyframes, float time, bool leftHand, Mtxf* matrix) {
    int i = 1;
    const Keyframe* current = keyframes + i;

    while (time >= current->duration) {
        time -= current->duration;
        i++;
        current++;
        if (current[2].final) {
            break;
        }
    }

    current = keyframes + i;
    if (current[2].final) {
        *matrix = RotationXYZ(current->rot);
        matrix->m[3][0] = current->pos[0];
        matrix->m[3][1] = current->pos[1];
        matrix->m[3][2] = current->pos[2];
        return false;
    }

    const float fraction = time / current->duration;
    const float tangent = current->interpParam;

    Quaternion q0 = Euler(current[-1].rot);
    const Quaternion q1 = Euler(current->rot);
    Quaternion q2 = Euler(current[1].rot);
    Quaternion q3 = Euler(current[2].rot);
    q2 = EnsureShortestPath(q1, q2);
    q3 = EnsureShortestPath(q2, q3);
    q0 = EnsureShortestPath(q1, q0);

    Quaternion rotation = SplineInterpolate(q0, q1, q2, q3, fraction);
    Vec3 position = CubicSpline(current[-1].pos, current->pos, current[1].pos, current[2].pos, fraction, tangent);

    if (leftHand) {
        position[0] = -position[0];
        rotation.w = -rotation.w;
        rotation.x = -rotation.x;
    }

    float rows[3][3];
    ToRotationMatrix(rotation, rows);
    *matrix = IdentityMtxf();
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            matrix->m[row][column] = rows[row][column];
        }
    }
    matrix->m[3][0] = position[0];
    matrix->m[3][1] = position[1];
    matrix->m[3][2] = position[2];
    return true;
}

} // namespace Frigate::Level
