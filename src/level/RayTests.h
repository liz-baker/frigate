#pragma once

#include <cstdint>

#include "level/Matrix.h"

namespace Frigate::Level {

// bgTestRayIntersectsBbox: whether the ray from `origin` along `dir` meets the
// box `min`..`max`, by the slab method without a division.
bool RayIntersectsBox(const Vec3& origin, const Vec3& dir, const int32_t (&min)[3], const int32_t (&max)[3]);

// intersectRayTriangle: where the line through `linePoint` along `dir` meets
// the triangle, and the triangle's unnormalised normal, when the meeting is
// ahead of `rayStart`. The corners are room-local positions moved by
// `offset` into the line's space; all of it is worked out in doubles.
struct TriangleHit {
    Vec3 position{};
    Vec3 normal{};
};
bool RayIntersectsTriangle(const int16_t (&v0)[3], const int16_t (&v1)[3], const int16_t (&v2)[3], const Vec3& offset,
                           const Vec3& rayStart, const Vec3& linePoint, const Vec3& dir, TriangleHit* hit);

// projectileTestPropBoundingSphere: whether the ray from `origin` along `dir`
// passes through the sphere of radius `radius` at `centre`, ahead of the
// origin.
bool RayIntersectsSphere(const Vec3& origin, const Vec3& dir, const Vec3& centre, float radius);

// modelTestRayIntersectsTransformedBBox: whether the ray from `pos` along
// `dir` meets the box `bounds` (xmin, xmax, ymin, ymax, zmin, zmax) under the
// matrix `matrix`.
bool RayIntersectsTransformedBox(const float (&bounds)[6], const Mtxf& matrix, const Vec3& pos, const Vec3& dir);

// sub_GAME_7F074CAC (the hit sphere test): whether the ray from `pos` along
// `dir` meets the sphere a hit sphere node (opcode 0x11) describes. `matrix`
// is the node's, `other` the matrix of the node it names (null for none).
// `flags` is the opcode word's high byte and `centre`, `radiusSquared`,
// `scale1` and `scale2` the node's data.
struct HitSphere {
    uint16_t flags = 0;
    float radiusSquared = 0.0f;
    Vec3 centre{};
    float scale1 = 0.0f;
    float scale2 = 0.0f;
};
bool RayIntersectsHitSphere(const HitSphere& sphere, const Mtxf& matrix, const Mtxf* other, const Vec3& pos,
                            const Vec3& dir);

} // namespace Frigate::Level
