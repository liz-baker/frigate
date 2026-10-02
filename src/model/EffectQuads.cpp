#include "model/EffectQuads.h"

#include <algorithm>
#include <cmath>

#include "model/Random.h"
#include "model/Trig.h"
#include "model/GameMath.h"

namespace Frigate {

namespace {

constexpr float kTau = 6.28318530717958647692f;

int16_t Truncate(float value) {
    return static_cast<int16_t>(static_cast<int32_t>(value));
}

void SetPosition(EffectVertex& vertex, float x, float y, float z) {
    vertex.position[0] = Truncate(x);
    vertex.position[1] = Truncate(y);
    vertex.position[2] = Truncate(z);
}

} // namespace

EffectQuad BuildGunfireQuad(const Mat4& mtx, const float offset[3], const float size[3], float modelScale,
                            const EffectImage* image, const std::function<uint32_t()>& random) {
    const auto next = [&random]() { return random ? random() : RandomGetNext(); };
    const auto& m = mtx.m;

    // The direction from the camera to the flash's offset, in the node's space.
    float toCamera[3] = {
        -(offset[0] * m[0][0] + offset[1] * m[1][0] + offset[2] * m[2][0] + m[3][0]),
        -(offset[0] * m[0][1] + offset[1] * m[1][1] + offset[2] * m[2][1] + m[3][1]),
        -(offset[0] * m[0][2] + offset[1] * m[1][2] + offset[2] * m[2][2] + m[3][2]),
    };
    const float distance = std::sqrt(toCamera[0] * toCamera[0] + toCamera[1] * toCamera[1] + toCamera[2] * toCamera[2]);
    if (distance > 0) {
        const float scale = 1 / (modelScale * distance);
        toCamera[0] *= scale;
        toCamera[1] *= scale;
        toCamera[2] *= scale;
    } else {
        toCamera[0] = 0;
        toCamera[1] = 0;
        toCamera[2] = 1 / modelScale;
    }

    // The two angles that turn the quad to face that direction.
    const float pitch = GameAcosf(toCamera[0] * m[1][0] + toCamera[1] * m[1][1] + toCamera[2] * m[1][2]);
    float yaw = GameAcosf(-(toCamera[0] * m[2][0] + toCamera[1] * m[2][1] + toCamera[2] * m[2][2]) / std::sin(pitch));
    if (-(toCamera[0] * m[0][0] + toCamera[1] * m[0][1] + toCamera[2] * m[0][2]) < 0) {
        yaw = kTau - yaw;
    }
    const float cosYaw = std::cos(yaw);
    const float sinYaw = std::sin(yaw);
    const float cosPitch = std::cos(pitch);
    const float sinPitch = std::sin(pitch);

    // 0.75 to 1.25 of its size.
    const float scale = 0.75f + static_cast<float>(next() % 128) * (1.0f / 256.0f);
    const float scaled[3] = { size[0] * scale, size[1] * scale, size[2] * scale };

    const float a = scaled[0] * cosYaw * 0.5f;
    const float b = scaled[2] * sinYaw * 0.5f;
    const float c = scaled[1] * sinPitch * 0.5f;
    const float d = scaled[0] * cosPitch * sinYaw * 0.5f;
    const float e = scaled[2] * cosPitch * cosYaw * 0.5f;

    const float originX = offset[0] - scaled[0] * 0.5f;
    const float originY = offset[1];
    const float originZ = offset[2];

    EffectQuad quad;
    SetPosition(quad[0], originX - a - d, originY - c, originZ + b - e);
    SetPosition(quad[1], originX - a + d, originY + c, originZ + b + e);
    SetPosition(quad[2], originX + a + d, originY + c, originZ - b + e);
    SetPosition(quad[3], originX + a - d, originY - c, originZ - b - e);

    if (image != nullptr) {
        // Texture coordinates turned about the image's centre by a random angle.
        const auto angle = static_cast<uint16_t>((next() * 1024) & 0xffff);
        const int32_t cosine = (Coss(angle) * image->width * 0xb5) >> 18;
        const int32_t sine = (Sins(angle) * image->width * 0xb5) >> 18;
        const int32_t centre = image->width << 4;
        quad[0].s = static_cast<int16_t>(centre - cosine);
        quad[0].t = static_cast<int16_t>(centre - sine);
        quad[1].s = static_cast<int16_t>(centre + sine);
        quad[1].t = static_cast<int16_t>(centre - cosine);
        quad[2].s = static_cast<int16_t>(centre + cosine);
        quad[2].t = static_cast<int16_t>(centre + sine);
        quad[3].s = static_cast<int16_t>(centre - sine);
        quad[3].t = static_cast<int16_t>(centre + cosine);
    }
    return quad;
}

EffectQuad BuildShadowQuad(const float position[2], const float size[2], float height, float modelScale,
                           bool zbufferEnabled, uint8_t alpha, const EffectImage* image) {
    float sizeX = size[0];
    float sizeY = size[1];
    if (height < 50.0f) {
        sizeX *= 1.25f;
        sizeY *= 1.25f;
    } else if (300.0f < height) {
        sizeX = 0.0f;
        sizeY = 0.0f;
    } else {
        sizeX *= (300.0f - height) / 200.0f;
        sizeY *= (300.0f - height) / 200.0f;
    }

    // Just above the ground with the z-buffer on, so it isn't hidden by it.
    const float y = zbufferEnabled ? (2.0f - height) / modelScale : (-height) / modelScale;

    EffectQuad quad;
    SetPosition(quad[0], position[0] - sizeX, y, position[1] - sizeY);
    SetPosition(quad[1], position[0] - sizeX, y, position[1] + sizeY);
    SetPosition(quad[2], position[0] + sizeX, y, position[1] + sizeY);
    SetPosition(quad[3], position[0] + sizeX, y, position[1] - sizeY);
    for (EffectVertex& vertex : quad) {
        vertex.colour[3] = alpha;
    }

    if (image != nullptr) {
        const auto right = static_cast<int16_t>((image->width << 5) - 1);
        const auto bottom = static_cast<int16_t>((image->height << 5) - 1);
        quad[1].s = right;
        quad[2].s = right;
        quad[2].t = bottom;
        quad[3].t = bottom;
    }
    return quad;
}

uint8_t ShadowAlpha(bool scaledByEnvironment, uint32_t envColour, int32_t shadowValue) {
    if (scaledByEnvironment) {
        return static_cast<uint8_t>(((envColour & 0xff) * static_cast<uint32_t>(shadowValue)) / 255);
    }
    return static_cast<uint8_t>(shadowValue);
}

} // namespace Frigate
