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

} // namespace Frigate::Level
