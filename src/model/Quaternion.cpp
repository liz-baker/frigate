#include "model/Quaternion.h"
#include "model/GameMath.h"

#include <cmath>

namespace Frigate {

namespace {
constexpr float kNearUnitThreshold = 0.99998999f;
constexpr float kEpsilon = 0.00001001f;

float Dot(const Quaternion& a, const Quaternion& b) {
    return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
}
} // namespace

Quaternion EulerToQuaternion(const JointRotation& rot) {
    float cx = std::cos(rot.x * 0.5f), sx = std::sin(rot.x * 0.5f);
    float cy = std::cos(rot.y * 0.5f), sy = std::sin(rot.y * 0.5f);
    float cz = std::cos(rot.z * 0.5f), sz = std::sin(rot.z * 0.5f);
    float cxcy = cx * cy, cxsy = cx * sy, sxcy = sx * cy, sxsy = sx * sy;

    Quaternion q;
    q.w = cxcy * cz + sxsy * sz;
    q.x = sxcy * cz - cxsy * sz;
    q.y = cxsy * cz + sxcy * sz;
    q.z = cxcy * sz - sxsy * cz;
    return q;
}

Quaternion SlerpFromIdentity(const Quaternion& q, float t) {
    float w = q.w;
    float identitySign = 1.0f;
    if (w < 0.0f) {
        w = -w;
        identitySign = -1.0f;
    }

    if (w > kNearUnitThreshold) {
        return Quaternion{
            q.w * t + (1.0f - t) * identitySign,
            q.x * t,
            q.y * t,
            q.z * t,
        };
    }

    float theta = GameAcosf(w);
    float sinTheta = std::sin(theta);
    float coeffQ = std::sin(t * theta) / sinTheta;
    float coeffIdentity = std::sin((1.0f - t) * theta) / sinTheta;
    return Quaternion{
        q.w * coeffQ + coeffIdentity * identitySign,
        q.x * coeffQ,
        q.y * coeffQ,
        q.z * coeffQ,
    };
}

Quaternion EnsureShortestPath(const Quaternion& q1, const Quaternion& q2) {
    if (Dot(q1, q2) < 0.0f) {
        return Quaternion{ -q2.w, -q2.x, -q2.y, -q2.z };
    }
    return q2;
}

Quaternion Slerp(const Quaternion& q1, const Quaternion& q2, float t) {
    float dot = Dot(q1, q2);

    if (dot < -1.0f + kEpsilon) {
        return Quaternion{
            (1.0f - t) * q1.w - q2.w * t,
            (1.0f - t) * q1.x - q2.x * t,
            (1.0f - t) * q1.y - q2.y * t,
            (1.0f - t) * q1.z - q2.z * t,
        };
    }

    if (dot <= 1.0f - kEpsilon) {
        float theta = GameAcosf(dot);
        float sinTheta = std::sin(theta);
        float coeffQ1 = std::sin((1.0f - t) * theta) / sinTheta;
        float coeffQ2 = std::sin(t * theta) / sinTheta;
        return Quaternion{
            coeffQ1 * q1.w + q2.w * coeffQ2,
            coeffQ1 * q1.x + q2.x * coeffQ2,
            coeffQ1 * q1.y + q2.y * coeffQ2,
            coeffQ1 * q1.z + q2.z * coeffQ2,
        };
    }

    return Quaternion{
        (1.0f - t) * q1.w + q2.w * t,
        (1.0f - t) * q1.x + q2.x * t,
        (1.0f - t) * q1.y + q2.y * t,
        (1.0f - t) * q1.z + q2.z * t,
    };
}

Quaternion Multiply(const Quaternion& lhs, const Quaternion& rhs) {
    return Quaternion{
        (lhs.w * rhs.w) - (lhs.x * rhs.x) - (lhs.y * rhs.y) - (lhs.z * rhs.z),
        (lhs.w * rhs.x) + (rhs.w * lhs.x) + (lhs.y * rhs.z) - (lhs.z * rhs.y),
        (lhs.w * rhs.y) + (rhs.w * lhs.y) + (lhs.z * rhs.x) - (lhs.x * rhs.z),
        (lhs.w * rhs.z) + (rhs.w * lhs.z) + (lhs.x * rhs.y) - (lhs.y * rhs.x),
    };
}

void ToRotationMatrix(const Quaternion& q, float m[3][3]) {
    const float scale = 2.0f / ((q.w * q.w) + (q.x * q.x) + (q.y * q.y) + (q.z * q.z));
    const float x = q.x * scale;
    const float y = q.y * scale;
    const float z = q.z * scale;
    const float wx = q.w * x;
    const float wy = q.w * y;
    const float wz = q.w * z;
    const float xx = q.x * x;
    const float xy = q.x * y;
    const float xz = q.x * z;
    const float yy = q.y * y;
    const float yz = q.y * z;
    const float zz = q.z * z;
    m[0][0] = 1.0f - (yy + zz);
    m[0][1] = xy + wz;
    m[0][2] = xz - wy;
    m[1][0] = xy - wz;
    m[1][1] = 1.0f - (xx + zz);
    m[1][2] = yz + wx;
    m[2][0] = xz + wy;
    m[2][1] = yz - wx;
    m[2][2] = 1.0f - (xx + yy);
}

namespace {

// quaternion_7F05BFD4: the logarithm of a unit quaternion.
Quaternion Logarithm(const Quaternion& q) {
    const float angle = GameAcosf(q.w);
    const float sine = std::sin(angle);
    if (sine == 0.0f) {
        return Quaternion{ 0.0f, 0.0f, 0.0f, 0.0f };
    }
    return Quaternion{ 0.0f, q.x * (angle / sine), q.y * (angle / sine), q.z * (angle / sine) };
}

// quaternion_7F05C068: the exponential of a pure quaternion.
Quaternion Exponential(const Quaternion& q) {
    const float angle = std::sqrt((q.x * q.x) + (q.y * q.y) + (q.z * q.z));
    if (angle == 0.0f) {
        return Quaternion{ 1.0f, 0.0f, 0.0f, 0.0f };
    }
    const float factor = std::sin(angle) / angle;
    return Quaternion{ std::cos(angle), q.x * factor, q.y * factor, q.z * factor };
}

// quaternion_7F05C138: the inner tangent at `q2`.
Quaternion InnerTangent(const Quaternion& q1, const Quaternion& q2, const Quaternion& q3) {
    const Quaternion conjugate{ q2.w, -q2.x, -q2.y, -q2.z };
    const Quaternion a = Logarithm(Multiply(conjugate, q1));
    const Quaternion b = Logarithm(Multiply(conjugate, q3));
    const Quaternion mean{
        -(a.w + b.w) * 0.25f,
        -(a.x + b.x) * 0.25f,
        -(a.y + b.y) * 0.25f,
        -(a.z + b.z) * 0.25f,
    };
    return Multiply(q2, Exponential(mean));
}

// quaternion_7F05C250.
Quaternion Squad(const Quaternion& q1, const Quaternion& q2, const Quaternion& q3, const Quaternion& q4, float t) {
    const float blend = (t + t) * (1.0f - t);
    const Quaternion outer = Slerp(q1, EnsureShortestPath(q1, q4), t);
    const Quaternion inner = Slerp(q2, EnsureShortestPath(q2, q3), t);
    return Slerp(outer, EnsureShortestPath(outer, inner), blend);
}

} // namespace

Quaternion SplineInterpolate(const Quaternion& q1, const Quaternion& q2, const Quaternion& q3, const Quaternion& q4,
                             float t) {
    const Quaternion a = InnerTangent(q1, q2, q3);
    const Quaternion b = InnerTangent(q2, q3, q4);
    return Squad(q2, a, b, q3, t);
}

} // namespace Frigate
