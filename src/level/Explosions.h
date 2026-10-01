#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "level/BulletImpacts.h"
#include "level/Matrix.h"
#include "level/PropList.h"
#include "level/Stan.h"

namespace Frigate::Level {

// s_explosiontype: one kind of explosion.
struct ExplosionType {
    float hrange;
    float vrange;
    float hchange;
    float vchange;
    float size;
    float range;
    float damageRange;
    int16_t duration;
    int16_t propagationRate;
    float flareSpeed;
    int16_t shrapnelBits;
    float shrapnelSize;
    float shrapnelScatter;
    float shrapnelHorizontalVelocity;
    float shrapnelVerticalVelocity;
    uint8_t smokeType;
    float damage;
};
constexpr int kExplosionTypeCount = 21;
const ExplosionType& ExplosionTypeOf(int type);

// s_smoketype.
struct SmokeType {
    int16_t duration;
    int16_t rateAppear;
    int16_t rateDissolve;
    int16_t size;
    float backgroundRotation;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    float foregroundRotation;
    int16_t propagatedClouds;
};
constexpr int kSmokeTypeCount = 11;
const SmokeType& SmokeTypeOf(int type);

// What an explosion reaches into the rest of the game for.
struct ExplosionHooks {
    // An object prop's obj->runtime_pos; false for a prop with no object.
    std::function<bool(int prop, Vec3* position)> objectPosition;
    // !(runtime_bitflags & 0x1000) && !(flags2 & 0x200400): whether the object
    // may take damage.
    std::function<bool(int prop)> objectDamageable;
    // maybe_detonate_object_and_its_children.
    std::function<void(int prop, float damage, const Vec3& position, int item, int owner)> detonateObject;
    // chrlvExplosionDamage.
    std::function<void(int prop, const Vec3& position, float damage)> damageChr;
    // record_damage_kills for the player: the damage and the direction it came
    // from on the ground.
    std::function<void(int prop, float damage, float dx, float dz, int owner)> damagePlayer;
};

// The explosions, the smoke they leave and the debris bullets kick up
// (explosion.c).
class Explosions {
  public:
    static constexpr int kMaxExplosions = 6;
    static constexpr int kExplosionParts = 40;
    static constexpr int kMaxSmoke = 20;
    static constexpr int kSmokeParts = 10;
    using RandomFn = std::function<uint32_t()>;

    struct Part {
        Vec3 position{};
        float size = 0.0f;
        float rotation = 0.0f;
        int16_t frame = 0;
    };
    struct Explosion {
        int prop = -1;
        int source = -1;
        std::array<Part, kExplosionParts> parts{};
        int16_t age = 0;
        int16_t nextDamage = 0;
        int8_t type = 0;
        // unk3CD: the explosion scorches the ground it stands on.
        int8_t scorches = 0;
        int8_t player = 0;
        Vec3 position{};
        int16_t room = 0;
    };
    struct SmokePart {
        Vec3 position{};
        float size = 0.0f;
        float rotation = 0.0f;
        float deltaRotation = 0.0f;
        float offset1 = 0.0f;
        float offset2 = 0.0f;
        float alpha = 0.0f;
        int16_t count = 0;
    };
    struct Smoke {
        int prop = -1;
        int16_t duration = 0;
        int16_t type = 0;
        std::array<SmokePart, kSmokeParts> parts{};
    };
    // FlyingParticles: a piece of debris.
    struct Particle {
        int32_t active = 0;
        Vec3 position{};
        Vec3 rotation{};
        Vec3 positionDrift{};
        Vec3 rotationDrift{};
        std::array<EffectVertex, 4> vertices{};
    };

    // What the explosions need of the world; set once.
    void Attach(PropList* props, Stan* stan, BulletImpacts* impacts, ExplosionHooks hooks) {
        props_ = props;
        stan_ = stan;
        impacts_ = impacts;
        hooks_ = std::move(hooks);
    }
    // The room a scorch is placed in needs the rooms' position and the scale.
    void SetRoomInfo(std::function<Vec3(int room)> roomPosition, float levelScale) {
        roomPosition_ = std::move(roomPosition);
        levelScale_ = levelScale;
    }

    // alloc_explosion_smoke_casing_scorch_impact_buffers' share: every buffer
    // empty. `particles` is the most pieces of debris at once.
    void Reset(int particles);

    // explosionCreate. `rooms` ends with 0xFF.
    void Create(int source, const Vec3& target, int targetTile, int type, int scorches, int player,
                const std::array<uint8_t, 8>& rooms, bool seedFromRooms, const RandomFn& random);
    // explosionCreateSmoke.
    void CreateSmoke(const Vec3& position, int tile, int smokeType, const std::array<uint8_t, 8>& rooms,
                     bool seedFromRooms);
    // explosionInitFlyingParticles.
    void InitParticle(const Vec3& spawn, float scatter, float horizontalDrift, float verticalDrift, float size,
                      const RandomFn& random);

    // explosionTick and explosionSmokeTick: ticks the prop's explosion or
    // smoke. True when it is over and its prop is to be freed (TICKOP_FREE).
    bool TickExplosion(int prop, int32_t clockTimer, const RandomFn& random);
    bool TickSmoke(int prop, int32_t clockTimer, const RandomFn& random);
    // explosionUpdateFlyingParticles.
    void UpdateParticles(int32_t clockTimer, const RandomFn& random);

    // explosionChrpropExplosionTick and explosionChrpropSmokeTick: the depth the
    // prop is drawn at, from the world-to-view matrix.
    void SetDepth(int prop, const Mtxf& worldToScreen);

    // cleanupExplosions: every explosion and smoke prop goes.
    void Clear();

    const Explosion* ExplosionOf(int prop) const;
    const Smoke* SmokeOf(int prop) const;
    const std::vector<Particle>& Particles() const {
        return particles_;
    }

  private:
    // explosionInflictDamage.
    void InflictDamage(int prop, Explosion& explosion, float horizontalRange, float verticalRange,
                       const RandomFn& random);
    // chraiGetPropRoomIds.
    std::vector<int> PropRoomIds(const Prop& prop) const;
    // propExecuteTickOperation(TICKOP_FREE).
    void FreeProp(int prop);

    PropList* props_ = nullptr;
    Stan* stan_ = nullptr;
    BulletImpacts* impacts_ = nullptr;
    ExplosionHooks hooks_;
    std::function<Vec3(int room)> roomPosition_;
    float levelScale_ = 1.0f;
    std::array<Explosion, kMaxExplosions> explosions_;
    std::array<Smoke, kMaxSmoke> smokes_;
    std::vector<Particle> particles_;
    // g_NumParticleEntries.
    size_t nextParticle_ = 0;
};

} // namespace Frigate::Level
