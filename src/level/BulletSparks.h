#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "level/Matrix.h"

namespace Frigate::Level {

// Which of the global image table's frame sets a spark draws from.
enum class SparkImages { Flare, Smoke, Scattered };

// s_bullet_spark: the puff and flash a bullet leaves where it lands.
struct BulletSpark {
    // age, lifetime (0 for a free slot) and the room it is drawn in (unk06).
    int32_t age = 0;
    int16_t lifetime = 0;
    int16_t room = 0;
    // unk08: how many frames of the set it moves through per tick.
    float frameRate = 0.0f;
    SparkImages images = SparkImages::Smoke;
    Vec3 position{};
    // unk1c and unk20: the quad's half extents along the camera's right and
    // up, and unk24 the radius they come from.
    float right = 0.0f;
    float up = 0.0f;
    float radius = 0.0f;
    std::array<uint8_t, 4> colour{};
};

// The bullet sparks (g_BulletSparkArray, in glass2.c).
class BulletSparks {
  public:
    static constexpr size_t kMax = 20;
    using RandomFn = std::function<uint32_t()>;

    // bullet_sparks_reset_all.
    void Reset();

    // bullet_spark_create: a spark of kind `type` (1 smoke, 3 debris, 4 and 6
    // flare) at `position` of size `size` in `room`. False when all are in use.
    bool Create(const Vec3& position, int type, float size, int room, const RandomFn& random);

    // bullet_sparks_update_all. (Nothing creates a moving spark, so only the
    // fixed ones age.)
    void Update(int32_t clockTimer);

    const std::array<BulletSpark, kMax>& Slots() const {
        return slots_;
    }

  private:
    // bullet_sparks_init.
    static void Init(BulletSpark* spark, const Vec3& position, int type, float size, int room, const RandomFn& random);

    std::array<BulletSpark, kMax> slots_;
};

} // namespace Frigate::Level
