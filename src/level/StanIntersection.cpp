#include "level/StanIntersection.h"

#include <cfloat>
#include <cmath>
#include <utility>

namespace Frigate::Level {

namespace {

// pointLineDistanceWithRadius: how far a circle at `origin` travelling along
// the unit vector `direction` goes before touching `point`.
float DistanceToPoint(float radius, const Vec2& origin, const Vec2& direction, const Vec2& point) {
    const float deltaX = point[0] - origin[0];
    const float deltaY = point[1] - origin[1];

    const float perpendicular = deltaY * direction[0] - deltaX * direction[1];
    float projected = deltaX * direction[0] + deltaY * direction[1];

    const float distanceSquared = (radius - perpendicular) * (radius + perpendicular);
    if (distanceSquared < 0.0f) {
        return FLT_MAX;
    }

    projected -= std::sqrt(distanceSquared);
    if (projected < 0.0f) {
        if (projected * projected + perpendicular * perpendicular <= radius * radius) {
            return 0.0f;
        }
        return FLT_MAX;
    }
    return projected;
}

} // namespace

float SegmentIntersectionFraction(const Vec2& start1, const Vec2& end1, const Vec2& start2, const Vec2& end2) {
    const float deltaX = start2[1] - end2[1];
    const float deltaY = end2[0] - start2[0];
    float factor = (start2[1] - start1[1]) * deltaY + (start2[0] - start1[0]) * deltaX;
    const float totalLength = (end1[1] - start1[1]) * deltaY + (end1[0] - start1[0]) * deltaX;

    if (totalLength == 0.0f) {
        return 1.0f;
    }

    factor /= totalLength;
    if (factor < 0.0f || factor > 1.0f) {
        return 1.0f;
    }
    return factor;
}

float CircleSegmentContactFraction(float radius, const Vec2& origin, const Vec2& lineStart, const Vec2& lineEnd,
                                   const Vec2& direction) {
    const float directionLength = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1]);
    if (directionLength == 0.0f) {
        return 1.0f;
    }

    const Vec2 normalizedDirection = { direction[0] * (1.0f / directionLength),
                                       direction[1] * (1.0f / directionLength) };

    Vec2 start = lineStart;
    Vec2 end = lineEnd;
    const float deltaX = end[0] - start[0];
    const float deltaY = end[1] - start[1];
    const float lineLength = std::sqrt(deltaX * deltaX + deltaY * deltaY);

    float contact;
    if (lineLength == 0.0f) {
        contact = DistanceToPoint(radius, origin, normalizedDirection, end);
    } else {
        const float inverseLength = 1.0f / lineLength;
        float normalX = deltaY * inverseLength;
        float normalY = -deltaX * inverseLength;

        float offsetX = radius * normalX;
        float offsetY = radius * normalY;
        if (offsetX * (origin[0] - start[0]) + offsetY * (origin[1] - start[1]) < 0.0f) {
            offsetX = -offsetX;
            offsetY = -offsetY;
        }

        const float parallelStartX = start[0] + offsetX;
        const float parallelStartY = start[1] + offsetY;
        const float parallelEndX = end[0] + offsetX;
        const float parallelEndY = end[1] + offsetY;

        float crossStart = (direction[1] * parallelStartX) - (parallelStartY * direction[0]);
        const float crossPoint = (origin[0] * direction[1]) - (origin[1] * direction[0]);
        float crossEnd = (direction[1] * parallelEndX) - (parallelEndY * direction[0]);

        if (crossEnd < crossStart) {
            std::swap(crossStart, crossEnd);
            std::swap(start, end);
            normalX = -normalX;
            normalY = -normalY;
        }

        if (crossEnd == crossStart) {
            contact = DistanceToPoint(radius, origin, normalizedDirection, start);
            const float other = DistanceToPoint(radius, origin, normalizedDirection, end);
            if (other < contact) {
                contact = other;
            }
        } else if (crossEnd < crossPoint) {
            contact = DistanceToPoint(radius, origin, normalizedDirection, end);
        } else if (crossPoint < crossStart) {
            contact = DistanceToPoint(radius, origin, normalizedDirection, start);
        } else {
            const float startProjection = normalX * (origin[0] - start[0]) + normalY * (origin[1] - start[1]);
            const float endProjection =
                normalX * (origin[0] + direction[0] - start[0]) + normalY * (origin[1] + direction[1] - start[1]);
            if (startProjection == endProjection) {
                return 1.0f;
            }
            contact = (startProjection - radius) * directionLength / (startProjection - endProjection);
        }
    }

    if (directionLength < contact) {
        return 1.0f;
    }
    if (contact < 0.0f) {
        return 0.0f;
    }
    return contact * (1.0f / directionLength);
}

} // namespace Frigate::Level
