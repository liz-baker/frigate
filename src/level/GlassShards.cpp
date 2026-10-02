#include "level/GlassShards.h"
#include "model/GameMath.h"

#include <cmath>

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kFraction = 1.0f / 4294967296.0f;
constexpr float kHorizontalVelocity = 1.5f;
constexpr float kVerticalVelocity = 3.0f;
constexpr float kAngularVelocity = 0.1f;

float Unit(const GlassShards::RandomFn& random) {
    return static_cast<float>(random()) * kFraction;
}

} // namespace

void GlassShards::Allocate(int capacity) {
    shards_.assign(static_cast<size_t>(capacity), GlassShard{});
    next_ = 0;
}

void GlassShards::Clear() {
    for (GlassShard& shard : shards_) {
        shard.active = 0;
    }
}

void GlassShards::Create(const Vec3& position, float rotX, float size, const RandomFn& random) {
    if (shards_.empty()) {
        return;
    }
    // Horizontal velocity in [-1, 1]; vertical in [-0.12, 1], biased up; the
    // other horizontal in [-1, 1].
    const float randSymmetricX = (2.0f * Unit(random)) - 1.0f;
    const float randBiasedY = (Unit(random) * 1.12f) - .12f;
    const float randSymmetricZ = (2.0f * Unit(random)) - 1.0f;

    GlassShard& shard = shards_[next_];
    shard.active = 1;
    shard.position = position;

    shard.velocity[0] = randSymmetricX * kHorizontalVelocity;
    shard.velocity[1] = randBiasedY * kVerticalVelocity;
    shard.velocity[2] = randSymmetricZ * kHorizontalVelocity;

    const auto corner = [&](float sign) { return ((Unit(random) * 0.5f) + 1.0f) * sign; };
    GlassShard::Corner& c1 = shard.corners[0];
    GlassShard::Corner& c2 = shard.corners[1];
    GlassShard::Corner& c3 = shard.corners[2];
    c1.position[0] = static_cast<int16_t>(corner(1.0f) * size);
    c1.position[1] = static_cast<int16_t>(corner(1.0f) * size);
    c1.position[2] = 0;

    c2.position[0] = static_cast<int16_t>(corner(1.0f) * size);
    c2.position[1] = static_cast<int16_t>(corner(1.0f) * -size);
    c2.position[2] = 0;

    c3.position[0] = static_cast<int16_t>(corner(1.0f) * -size);
    c3.position[1] = static_cast<int16_t>(corner(1.0f) * -size);
    c3.position[2] = 0;

    for (GlassShard::Corner& c : shard.corners) {
        c.texture = { 0, 0 };
    }

    // A gradient over the corners, for the environment mapping.
    c1.colour = { 5, 5, 0x7E, 0xFF };
    c2.colour = { 5, 0xFB, 0x7E, 0xFF };
    c3.colour = { 0xFB, 0xFB, 0x7E, 0xFF };

    shard.rotation[0] = rotX;
    shard.rotation[1] = 0.0f;
    shard.rotation[2] = Unit(random) * kTau;

    // A random spin.
    shard.angularVelocity[0] = Unit(random) * kAngularVelocity;
    shard.angularVelocity[1] = Unit(random) * kAngularVelocity;
    shard.angularVelocity[2] = Unit(random) * kAngularVelocity;

    next_++;
    if (next_ >= shards_.size()) {
        next_ = 0;
    }
}

void GlassShards::ShatterWindow(const Vec3& position, const Vec3& xAxis, const Vec3& yAxis, const Vec3& zAxis,
                                float xmin, float xmax, float ymin, float ymax, const RandomFn& random) {
    Vec3 xnorm = xAxis;
    float len = xnorm[0] * xnorm[0];
    len = std::sqrt((xnorm[2] * xnorm[2]) + (len + (xnorm[1] * xnorm[1])));
    xnorm[0] *= 1.0f / len;
    xnorm[1] *= 1.0f / len;
    xnorm[2] *= 1.0f / len;
    xmin *= len;
    xmax *= len;

    Vec3 ynorm = yAxis;
    len = ynorm[0] * ynorm[0];
    len = std::sqrt((ynorm[2] * ynorm[2]) + (len + (ynorm[1] * ynorm[1])));
    ynorm[0] *= 1.0f / len;
    ynorm[1] *= 1.0f / len;
    ynorm[2] *= 1.0f / len;
    ymin *= len;
    ymax *= len;

    const float angle = GameAtan2f(zAxis[0], zAxis[2]);
    const float width = xmax - xmin;
    const float height = ymax - ymin;
    const float shardSize = std::sqrt((width * height) / static_cast<float>(static_cast<int>(shards_.size()) / 2));
    const int step = static_cast<int>(shardSize);
    if (step <= 0) {
        return;
    }
    const float half = static_cast<float>(step >> 1);
    Vec3 basePosition;
    for (size_t axis = 0; axis < 3; axis++) {
        basePosition[axis] = (position[axis] + ((xmin + half) * xnorm[axis])) + (ynorm[axis] * (ymin + half));
    }

    // TODO(https://github.com/liz-baker/frigate/issues/41): the sound of
    // breaking glass.
    const int xcount = static_cast<int>(width / static_cast<float>(step));
    const int ycount = static_cast<int>(height / static_cast<float>(step));
    for (int y = 0; y < ycount; y++) {
        for (int x = 0; x < xcount; x++) {
            Vec3 shardPosition;
            for (size_t axis = 0; axis < 3; axis++) {
                shardPosition[axis] =
                    (basePosition[axis] + ((static_cast<float>(x) * static_cast<float>(step)) * xnorm[axis])) +
                    (ynorm[axis] * (static_cast<float>(y) * static_cast<float>(step)));
            }
            Create(shardPosition, angle, (((static_cast<float>(random()) * 2.3283064e-10f) * 0.7f) + 0.1f) * shardSize,
                   random);
        }
    }
}

void GlassShards::Update(int32_t clockTimer) {
    const float timer = clockTimer < 15 ? static_cast<float>(clockTimer) : 15.0f;
    for (GlassShard& shard : shards_) {
        if (shard.active <= 0) {
            continue;
        }
        shard.active += static_cast<int32_t>(timer);
        for (size_t axis = 0; axis < 3; axis++) {
            shard.rotation[axis] += shard.angularVelocity[axis] * timer;
        }
        shard.position[0] += shard.velocity[0] * timer;
        shard.position[2] += shard.velocity[2] * timer;
        for (int j = 0; j < static_cast<int32_t>(timer); j++) {
            shard.position[1] += shard.velocity[1];
            shard.velocity[1] -= 0.1f;
        }
        if (shard.active >= 0x96) {
            shard.active = 0;
        }
        if (shard.position[1] < -30000.0f || shard.position[1] > 30000.0f) {
            shard.active = 0;
        }
    }
}

} // namespace Frigate::Level
