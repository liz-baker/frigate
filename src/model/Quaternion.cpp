#include "model/Quaternion.h"

#include <cmath>

namespace Frigate {

namespace {
constexpr float kNearUnitThreshold = 0.99998999f;
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

} // namespace Frigate
