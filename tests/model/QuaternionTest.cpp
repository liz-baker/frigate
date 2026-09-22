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

    // Slerp between two quaternions at t=0/t=1 yields each endpoint, and
    // halfway between a 0 and 90-degree Z rotation yields the 45-degree one
    // -- the same shape as the identity-anchored case above, generalized.
    {
        JointRotation none;
        JointRotation quarterTurn;
        quarterTurn.z = kPi / 2.0f;
        Quaternion q1 = EulerToQuaternion(none);
        Quaternion q2 = EulerToQuaternion(quarterTurn);

        Quaternion atStart = Slerp(q1, q2, 0.0f);
        Check(Near(atStart.w, q1.w) && Near(atStart.z, q1.z), "slerp t=0 yields q1");

        Quaternion atEnd = Slerp(q1, q2, 1.0f);
        Check(Near(atEnd.w, q2.w) && Near(atEnd.z, q2.z), "slerp t=1 yields q2");

        Quaternion half = Slerp(q1, q2, 0.5f);
        Check(Near(half.w, std::cos(kPi / 8.0f)) && Near(half.z, std::sin(kPi / 8.0f)),
              "slerp halfway between 0 and 90 degrees yields 45 degrees");
    }

    // EnsureShortestPath negates q2 when the two are more than 90 degrees
    // apart (a negative dot product), leaving it unchanged otherwise. A pure
    // Z rotation's w is cos(angle/2), so a 342-degree turn (half-angle just
    // under pi) has w just below -1 -- a negative dot against identity.
    {
        Quaternion identity = EulerToQuaternion(JointRotation{});
        JointRotation almostFullTurn;
        almostFullTurn.z = kPi * 1.9f;
        Quaternion q2 = EulerToQuaternion(almostFullTurn);
        Check(q2.w < 0.0f, "sanity: the almost-full-turn quaternion's w is negative");

        Quaternion fixed = EnsureShortestPath(identity, q2);
        Check(fixed.w == -q2.w && fixed.z == -q2.z, "a negative dot product negates q2");

        Quaternion unchanged = EnsureShortestPath(identity, identity);
        Check(unchanged.w == identity.w && unchanged.x == identity.x, "a non-negative dot product leaves q2 unchanged");
    }

    if (gFailures == 0) {
        std::printf("QuaternionTest: all checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
