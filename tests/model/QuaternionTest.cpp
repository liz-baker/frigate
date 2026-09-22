#include "model/Quaternion.h"

#include <cmath>
#include <cstdio>

using namespace Frigate;

namespace {

int gFailures = 0;

constexpr float kPi = 3.141592653589793f;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) < eps;
}

} // namespace

int main() {
    // A zero rotation is the identity quaternion.
    {
        Quaternion q = EulerToQuaternion(JointRotation{});
        Check(Near(q.w, 1.0f) && Near(q.x, 0.0f) && Near(q.y, 0.0f) && Near(q.z, 0.0f), "zero rotation is identity");
    }

    // A pure Z rotation matches the standard single-axis quaternion:
    // (cos(angle/2), 0, 0, sin(angle/2)).
    {
        JointRotation rot;
        rot.z = kPi / 2.0f;
        Quaternion q = EulerToQuaternion(rot);
        Check(Near(q.w, std::cos(kPi / 4.0f)) && Near(q.x, 0.0f) && Near(q.y, 0.0f) && Near(q.z, std::sin(kPi / 4.0f)),
              "pure Z rotation matches the single-axis quaternion formula");
    }

    // Slerping from identity by t=0 yields identity, by t=1 yields q itself.
    {
        JointRotation rot;
        rot.z = kPi / 2.0f;
        Quaternion q = EulerToQuaternion(rot);

        Quaternion atStart = SlerpFromIdentity(q, 0.0f);
        Check(Near(atStart.w, 1.0f) && Near(atStart.z, 0.0f), "t=0 yields identity");

        Quaternion atEnd = SlerpFromIdentity(q, 1.0f);
        Check(Near(atEnd.w, q.w) && Near(atEnd.z, q.z), "t=1 yields q itself");
    }

    // Slerping halfway to a 90-degree Z rotation yields the 45-degree one.
    {
        JointRotation rot;
        rot.z = kPi / 2.0f;
        Quaternion q = EulerToQuaternion(rot);
        Quaternion half = SlerpFromIdentity(q, 0.5f);
        Check(Near(half.w, std::cos(kPi / 8.0f)) && Near(half.z, std::sin(kPi / 8.0f)),
              "halfway slerp from identity yields half the rotation angle");
    }

    if (gFailures == 0) {
        std::printf("QuaternionTest: all checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
