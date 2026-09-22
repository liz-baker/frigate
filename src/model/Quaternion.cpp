#include "model/Quaternion.h"

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

    float theta = std::acos(w);
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
        float theta = std::acos(dot);
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

} // namespace Frigate
