#pragma once

#include <array>

namespace Frigate::Level {

using Vec2 = std::array<float, 2>;

// calculateSegmentIntersectionFraction: how far along start1..end1 (0..1) it
// crosses the line through start2..end2; 1 when it does not.
float SegmentIntersectionFraction(const Vec2& start1, const Vec2& end1, const Vec2& start2, const Vec2& end2);

// calculateRayToSegmentIntersectionNormalized: how far along `direction`, as a
// fraction of its length, a circle of `radius` at `origin` first touches the
// segment lineStart..lineEnd; 1 when it does not within the direction's length.
float CircleSegmentContactFraction(float radius, const Vec2& origin, const Vec2& lineStart, const Vec2& lineEnd,
                                   const Vec2& direction);

// getRotationalDirectionBetween: 1 when b is anticlockwise of a, -1 when
// clockwise (or opposite), 0 for the same direction with |a| >= |b|.
int RotationalDirection(float ax, float az, float bx, float bz);

// doSegmentsIntersect: whether start1..end1 and start2..end2 cross.
bool SegmentsIntersect(const Vec2& start1, const Vec2& end1, const Vec2& start2, const Vec2& end2);

// stanGetSignedPointLineDistance: the perpendicular distance from `p` to the
// line through `a` and `b`, signed by the side `p` is on; the distance to `b`
// when the two coincide.
float SignedPointLineDistance(const Vec2& a, const Vec2& b, const Vec2& p);

// distBetweenPoints2d.
float Distance2d(const Vec2& a, const Vec2& b);

// stanPointProjectsOntoEdge: whether `p`'s projection onto the line through
// `a` and `b` falls within the edge.
bool PointProjectsOntoEdge(const Vec2& a, const Vec2& b, const Vec2& p);

} // namespace Frigate::Level
