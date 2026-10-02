#include <cstdio>

#include "level/RayTests.h"

using namespace Frigate::Level;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

void TestBoxHitAndMiss() {
    const int32_t min[3] = { -10, -10, 90 };
    const int32_t max[3] = { 10, 10, 110 };
    Check(RayIntersectsBox({ 0, 0, 0 }, { 0, 0, 1 }, min, max), "a ray straight at a box hits it");
    Check(!RayIntersectsBox({ 0, 0, 0 }, { 0, 0, -1 }, min, max), "a ray away from a box misses it");
    Check(!RayIntersectsBox({ 0, 0, 0 }, { 1, 0, 0.1f }, min, max), "a ray that passes beside a box misses it");
    Check(RayIntersectsBox({ 0, 0, 0 }, { 0.05f, 0.05f, 1 }, min, max), "a ray that clips a box's face hits it");
}

void TestTriangleHit() {
    // A triangle in the plane z = 100, facing the origin.
    const int16_t v0[3] = { -10, -10, 100 };
    const int16_t v1[3] = { 10, -10, 100 };
    const int16_t v2[3] = { 0, 10, 100 };
    TriangleHit hit;
    Check(RayIntersectsTriangle(v0, v1, v2, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 1000 }, { 0, 0, 1000 }, &hit),
          "a line through a triangle's middle hits it");
    Check(hit.position[2] == 100.0f && hit.position[0] == 0.0f, "the hit is where the line meets the plane");
    Check(!RayIntersectsTriangle(v0, v1, v2, { 0, 0, 0 }, { 50, 0, 0 }, { 50, 0, 1000 }, { 0, 0, 1000 }, &hit),
          "a line that misses the triangle's edges misses it");
    Check(!RayIntersectsTriangle(v0, v1, v2, { 0, 0, 0 }, { 0, 0, 200 }, { 0, 0, 1000 }, { 0, 0, 1000 }, &hit),
          "a triangle behind the ray's start is not hit");
    Check(!RayIntersectsTriangle(v0, v1, v2, { 0, 0, -200 }, { 0, 0, 0 }, { 0, 0, 1000 }, { 0, 0, 1000 }, &hit),
          "the offset moves the triangle behind the ray");
}

void TestSphere() {
    Check(RayIntersectsSphere({ 0, 0, 0 }, { 0, 0, 1 }, { 0, 0, 100 }, 10), "a ray through a sphere's middle hits it");
    Check(!RayIntersectsSphere({ 0, 0, 0 }, { 0, 0, 1 }, { 0, 50, 100 }, 10), "a ray that passes wide misses it");
    Check(!RayIntersectsSphere({ 0, 0, 0 }, { 0, 0, 1 }, { 0, 0, -100 }, 10), "a sphere behind the ray is not hit");
}

void TestTransformedBox() {
    const float bounds[6] = { -10, 10, -10, 10, -10, 10 };
    Mtxf matrix = IdentityMtxf();
    matrix.m[3][2] = 100.0f;
    Check(RayIntersectsTransformedBox(bounds, matrix, { 0, 0, 0 }, { 0, 0, 1 }), "a ray at a moved box hits it");
    Check(!RayIntersectsTransformedBox(bounds, matrix, { 0, 0, 0 }, { 1, 0, 0 }), "a ray off to the side misses it");
}

void TestHitSphere() {
    HitSphere sphere;
    sphere.radiusSquared = 100.0f;
    Mtxf matrix = IdentityMtxf();
    matrix.m[3][2] = 100.0f;
    Check(RayIntersectsHitSphere(sphere, matrix, nullptr, { 0, 0, 0 }, { 0, 0, 1 }),
          "a ray at a hit sphere at its node's position hits it");
    sphere.flags = 0x100;
    sphere.centre = { 0, 50, 0 };
    Check(!RayIntersectsHitSphere(sphere, matrix, nullptr, { 0, 0, 0 }, { 0, 0, 1 }),
          "a stored centre moves the sphere off the ray");
    sphere.flags = 0x200;
    Mtxf other = IdentityMtxf();
    other.m[3][2] = 100.0f;
    other.m[3][1] = 100.0f;
    Check(!RayIntersectsHitSphere(sphere, matrix, &other, { 0, 0, 0 }, { 0, 0, 1 }),
          "the middle between two nodes can miss the ray");
}

} // namespace

int main() {
    TestBoxHitAndMiss();
    TestTriangleHit();
    TestSphere();
    TestTransformedBox();
    TestHitSphere();
    return gFailures == 0 ? 0 : 1;
}
