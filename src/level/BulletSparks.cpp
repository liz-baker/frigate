#include "level/BulletSparks.h"
#include "model/GameMath.h"

#include <cmath>

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kSqrt2 = 1.41412f;
constexpr float kRandomFraction = 1.0f / 4294967295.0f;

// g_BulletSparkColors.
constexpr std::array<std::array<uint8_t, 4>, 8> kColours = { {
    { 0xFF, 0xFF, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xC8, 0xFF },
    { 0xFF, 0x00, 0x00, 0xFF },
    { 0xFF, 0xFF, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xFF, 0xFF },
    { 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
} };

} // namespace

void BulletSparks::Reset() {
    for (BulletSpark& spark : slots_) {
        spark.lifetime = 0;
        spark.age = 0;
    }
}

void BulletSparks::Init(BulletSpark* spark, const Vec3& position, int type, float size, int room,
                        const RandomFn& random) {
    float angle = static_cast<float>(random());
    angle *= kRandomFraction;
    angle *= kTau;

    spark->age = 0;
    spark->room = static_cast<int16_t>(room);

    if (type == 4) {
        spark->lifetime = 1;
        spark->frameRate = 1.0f;
        spark->images = SparkImages::Flare;
    } else if (type == 1) {
        spark->lifetime = 11;
        spark->frameRate = 0.5f;
        spark->images = SparkImages::Smoke;
    } else if (type == 3) {
        spark->lifetime = 9;
        spark->frameRate = 0.5f;
        spark->images = SparkImages::Scattered;
    } else if (type == 6) {
        spark->lifetime = 100;
        spark->frameRate = 0.0f;
        spark->images = SparkImages::Flare;
    } else {
        spark->lifetime = 11;
        spark->frameRate = 0.5f;
        spark->images = SparkImages::Smoke;
    }

    spark->colour = kColours[static_cast<size_t>(type)];
    spark->position = position;

    size *= 1.0f + (static_cast<float>(random()) * kRandomFraction * 0.25f);
    size *= kSqrt2;
    spark->radius = size;

    spark->right = GameCosf(angle) * size;
    spark->up = GameSinf(angle) * size;
}

bool BulletSparks::Create(const Vec3& position, int type, float size, int room, const RandomFn& random) {
    for (BulletSpark& spark : slots_) {
        if (spark.lifetime == 0) {
            Init(&spark, position, type, size, room, random);
            return true;
        }
    }
    return false;
}

void BulletSparks::Update(int32_t clockTimer) {
    for (BulletSpark& spark : slots_) {
        if (spark.lifetime > 0) {
            spark.age += clockTimer;
            if (spark.age >= 0 && spark.age >= spark.lifetime) {
                spark.lifetime = 0;
            }
        }
    }
}

} // namespace Frigate::Level
