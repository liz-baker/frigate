#include "level/Explosions.h"
#include "game/Sound.h"
#include "model/GameMath.h"

#include <cmath>

namespace Frigate::Level {

namespace {

constexpr float kTau = 6.2831855f;
constexpr float kFraction = 1.0f / 4294967296.0f;

// g_ExplosionTypes (the NTSC table): hrange, vrange, hchange, vchange, size,
// range, damage range, duration, propagation rate, flare speed, shrapnel bits,
// shrapnel size, scatter, horizontal and vertical velocity, smoke type, sound, damage.
constexpr std::array<ExplosionType, kExplosionTypeCount> kExplosionTypes = { {
    { 0.1f, 0.1f, 0.0f, 0.0f, 0.1f, 0.0f, 0.0f, 1, 1, 1.0f, 0, 0.1f, 0.0f, 0.0f, 0.0f, 0, AudioDriver::kSfxNothing,
      0.0f },
    { 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 30, 1, 1.0f, 10, 5.0f, 0.0f, 2.0f, 6.0f, 7, AudioDriver::kSfxNothing,
      0.0f },
    { 20.0f, 20.0f, 0.0f, 0.0f, 30.0f, 50.0f, 50.0f, 80, 1, 3.0f, 40, 6.0f, 5.0f, 0.7f, 6.0f, 2,
      AudioDriver::kSfxExplosion1b, 0.125f },
    { 50.0f, 50.0f, 0.0f, 0.0f, 50.0f, 100.0f, 100.0f, 90, 1, 4.0f, 50, 6.0f, 10.0f, 1.0f, 6.0f, 2,
      AudioDriver::kSfxExplosion1c, 0.5f },
    { 60.0f, 80.0f, 1.0f, 0.3f, 100.0f, 150.0f, 280.0f, 120, 2, 5.0f, 80, 8.0f, 30.0f, 2.0f, 6.0f, 1,
      AudioDriver::kSfxExplosion4a, 1.0f },
    { 60.0f, 120.0f, 1.0f, 0.3f, 150.0f, 200.0f, 310.0f, 120, 2, 5.0f, 80, 8.0f, 30.0f, 2.0f, 6.0f, 1,
      AudioDriver::kSfxExplosion4a, 2.0f },
    { 20.0f, 20.0f, 0.0f, 0.0f, 22.0f, 40.0f, 40.0f, 80, 1, 3.0f, 40, 6.0f, 5.0f, 0.7f, 6.0f, 2,
      AudioDriver::kSfxExplosion1b, 0.5f },
    { 35.0f, 40.0f, 0.0f, 0.0f, 35.0f, 70.0f, 70.0f, 90, 1, 4.0f, 50, 6.0f, 10.0f, 1.0f, 6.0f, 2,
      AudioDriver::kSfxExplosion1c, 1.0f },
    { 50.0f, 80.0f, 1.0f, 0.3f, 50.0f, 100.0f, 220.0f, 120, 2, 5.0f, 80, 8.0f, 30.0f, 2.0f, 6.0f, 1,
      AudioDriver::kSfxExplosion4a, 2.0f },
    { 60.0f, 120.0f, 1.0f, 0.3f, 50.0f, 130.0f, 230.0f, 120, 2, 5.0f, 80, 8.0f, 30.0f, 2.0f, 6.0f, 1,
      AudioDriver::kSfxExplosion4a, 2.0f },
    { 40.0f, 40.0f, 0.4f, 0.2f, 70.0f, 100.0f, 180.0f, 170, 4, 5.0f, 120, 6.0f, 30.0f, 2.5f, 6.0f, 4,
      AudioDriver::kSfxExplosion5a, 1.0f },
    { 50.0f, 50.0f, 0.6f, 0.4f, 100.0f, 150.0f, 260.0f, 180, 1, 4.0f, 150, 6.0f, 30.0f, 3.0f, 6.0f, 4,
      AudioDriver::kSfxExplosion4a, 2.0f },
    { 70.0f, 60.0f, 1.0f, 0.6f, 150.0f, 225.0f, 320.0f, 180, 2, 5.0f, 150, 6.0f, 30.0f, 4.0f, 12.0f, 5,
      AudioDriver::kSfxExplosion4a, 4.0f },
    { 80.0f, 60.0f, 2.0f, 0.7f, 200.0f, 300.0f, 480.0f, 180, 2, 5.0f, 200, 6.0f, 30.0f, 6.0f, 15.0f, 6,
      AudioDriver::kSfxExplosion4b, 4.0f },
    { 50.0f, 50.0f, 0.0f, 0.0f, 120.0f, 200.0f, 400.0f, 150, 4, 4.0f, 150, 6.0f, 30.0f, 3.0f, 6.0f, 4,
      AudioDriver::kSfxExplosion4b, 4.0f },
    { 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1, 1, 1.0f, 150, 6.0f, 30.0f, 2.5f, 6.0f, 7,
      AudioDriver::kSfxExplosion2b, 0.0f },
    { 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1, 1, 1.0f, 100, 6.0f, 30.0f, 2.5f, 6.0f, 7,
      AudioDriver::kSfxExplosion2b, 0.0f },
    { 80.0f, 60.0f, 15.0f, 5.0f, 1500.0f, 2200.0f, 3600.0f, 300, 1, 2.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0,
      AudioDriver::kSfxExplosion4b, 4.0f },
    { 80.0f, 60.0f, 3.0f, 1.0f, 300.0f, 450.0f, 640.0f, 60, 1, 2.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0,
      AudioDriver::kSfxExplosion4b, 4.0f },
    { 90.0f, 75.0f, 2.5f, 0.87f, 250.0f, 375.0f, 600.0f, 180, 2, 5.0f, 200, 6.0f, 30.0f, 6.0f, 15.0f, 6,
      AudioDriver::kSfxExplosion4b, 4.0f },
    { 160.0f, 120.0f, 6.0f, 2.0f, 600.0f, 450.0f, 640.0f, 60, 1, 2.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0,
      AudioDriver::kSfxExplosion4b, 4.0f },
} };

// g_SmokeTypes (the NTSC table): duration, rate of appearing, rate of
// dissolving, size, background and foreground rotation rates, colour, and the
// ticks the smoke ends over.
constexpr std::array<SmokeType, kSmokeTypeCount> kSmokeTypes = { {
    { 1, 60, 99, 0, 0.0f, 128, 128, 128, 0.3f, 180 },
    { 480, 60, 45, 60, 0.02f, 80, 80, 96, 0.3f, 180 },
    { 480, 60, 50, 20, 0.01f, 128, 128, 128, 0.3f, 180 },
    { 640, 60, 120, 100, 0.01f, 192, 192, 192, 0.3f, 180 },
    { 640, 60, 60, 80, 0.02f, 64, 64, 64, 0.3f, 180 },
    { 770, 60, 50, 190, 0.15f, 64, 64, 64, 0.3f, 180 },
    { 900, 60, 70, 300, 0.01f, 64, 64, 64, 0.3f, 180 },
    { 60, 60, 8, 15, 0.03f, 255, 255, 255, 0.3f, 180 },
    { 20, 1, 6, 30, 0.03f, 255, 255, 255, 2.0f, 30 },
    { 25, 1, 7, 16, 0.03f, 224, 224, 224, 3.0f, 30 },
    { 900, 60, 70, 900, 0.01f, 64, 64, 64, 0.3f, 180 },
} };

float Fraction(const Explosions::RandomFn& random) {
    return static_cast<float>(random()) * 2.3283064e-10f;
}

float Unit(const Explosions::RandomFn& random) {
    return static_cast<float>(random()) * kFraction;
}

} // namespace

const ExplosionType& ExplosionTypeOf(int type) {
    return kExplosionTypes[static_cast<size_t>(type)];
}

const SmokeType& SmokeTypeOf(int type) {
    return kSmokeTypes[static_cast<size_t>(type)];
}

void Explosions::Reset(int particles) {
    for (Explosion& explosion : explosions_) {
        explosion = Explosion{};
    }
    for (Smoke& smoke : smokes_) {
        smoke = Smoke{};
    }
    particles_.assign(static_cast<size_t>(particles), Particle{});
    nextParticle_ = 0;
}

void Explosions::Clear() {
    for (Explosion& explosion : explosions_) {
        if (explosion.prop >= 0) {
            FreeProp(explosion.prop);
            explosion.prop = -1;
        }
    }
    for (Smoke& smoke : smokes_) {
        if (smoke.prop >= 0) {
            FreeProp(smoke.prop);
            smoke.prop = -1;
        }
    }
}

void Explosions::FreeProp(int prop) {
    props_->DeregisterRooms(prop);
    props_->Delist(prop);
    props_->Disable(prop);
    props_->Free(prop);
}

const Explosions::Explosion* Explosions::ExplosionOf(int prop) const {
    const Prop& record = (*props_)[prop];
    if (record.type != PropType::Explosion || record.object < 0) {
        return nullptr;
    }
    return &explosions_[static_cast<size_t>(record.object)];
}

void Explosions::EndFlares(int prop) {
    const Prop& record = (*props_)[prop];
    if (record.type != PropType::Explosion || record.object < 0) {
        return;
    }
    Explosion& explosion = explosions_[static_cast<size_t>(record.object)];
    const auto limit = static_cast<int32_t>(ExplosionTypeOf(explosion.type).flareSpeed * 15.0f);
    for (Part& part : explosion.parts) {
        if (limit < part.frame) {
            part.frame = 0;
        }
    }
}

const Explosions::Smoke* Explosions::SmokeOf(int prop) const {
    const Prop& record = (*props_)[prop];
    if (record.type != PropType::Smoke || record.object < 0) {
        return nullptr;
    }
    return &smokes_[static_cast<size_t>(record.object)];
}

void Explosions::Create(int source, const Vec3& target, int targetTile, int type, int scorches, int player,
                        const std::array<uint8_t, 8>& rooms, bool seedFromRooms, const RandomFn& random) {
    const ExplosionType& explosionType = ExplosionTypeOf(type);

    int slot = -1;
    for (int i = 0; i < kMaxExplosions; i++) {
        if (explosions_[static_cast<size_t>(i)].prop < 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return;
    }
    Explosion& explosion = explosions_[static_cast<size_t>(slot)];

    if (explosionType.sound > 0) {
        Game::Sound::PostVolumeAtPositionDefault(Game::Sound::PlaySfx(explosionType.sound), target);
    }
    // TODO(https://github.com/liz-baker/frigate/issues/136): the screen shake the
    // camera adds while an explosion is going (g_NumExplosionEntries).
    const int prop = props_->Allocate();
    if (prop < 0) {
        return;
    }

    Prop& record = (*props_)[prop];
    record.type = PropType::Explosion;
    record.flags |= PropFlag::OnScreen;
    record.object = slot;
    record.stan = targetTile;
    record.position = target;
    int i = 0;
    while (rooms[static_cast<size_t>(i)] != kNoRoom && i < 7) {
        record.rooms[static_cast<size_t>(i)] = rooms[static_cast<size_t>(i)];
        i++;
    }
    record.rooms[static_cast<size_t>(i)] = kNoRoom;

    if (seedFromRooms) {
        record.flags |= PropFlag::SeedFromRooms;
    }

    props_->ActivateThisFrame(prop);
    props_->Enable(prop);

    explosion.type = static_cast<int8_t>(type);
    explosion.age = 0;
    explosion.nextDamage = -1;
    explosion.scorches = static_cast<int8_t>(scorches);
    explosion.prop = prop;
    explosion.source = source;
    explosion.player = static_cast<int8_t>(player);

    if (scorches != 0) {
        const Prop* sourceRecord = source >= 0 ? &(*props_)[source] : nullptr;
        if (sourceRecord != nullptr && sourceRecord->stan != kNoTile) {
            explosion.position = {
                sourceRecord->position[0],
                stan_->PositionY(sourceRecord->stan, sourceRecord->position[0], sourceRecord->position[2]) + 4.0f,
                sourceRecord->position[2]
            };
            explosion.room = static_cast<int16_t>(stan_->TileRoom(sourceRecord->stan));
        } else {
            explosion.position = { target[0], stan_->PositionY(targetTile, target[0], target[2]) + 4.0f, target[2] };
            explosion.room = static_cast<int16_t>(stan_->TileRoom(targetTile));
        }
    } else {
        explosion.position[0] = 999999.9f;
    }

    const float size = ((Fraction(random) * 0.5f) + 1.0f) * explosionType.size;
    const float angle = Fraction(random) * kTau;

    explosion.parts[0].size = GameCosf(angle) * size;
    explosion.parts[0].rotation = GameSinf(angle) * size;
    explosion.parts[0].frame = 1;
    explosion.parts[0].position = target;

    for (int bit = 0; bit < explosionType.shrapnelBits; bit++) {
        InitParticle(target, explosionType.shrapnelScatter, explosionType.shrapnelHorizontalVelocity,
                     explosionType.shrapnelVerticalVelocity, explosionType.shrapnelSize, random);
    }
}

void Explosions::CreateSmoke(const Vec3& position, int tile, int smokeType, const std::array<uint8_t, 8>& rooms,
                             bool seedFromRooms) {
    int slot = -1;
    for (int i = 0; i < kMaxSmoke; i++) {
        if (smokes_[static_cast<size_t>(i)].prop < 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return;
    }

    const int prop = props_->Allocate();
    if (prop < 0) {
        return;
    }

    Prop& record = (*props_)[prop];
    record.type = PropType::Smoke;
    record.flags |= PropFlag::OnScreen;
    record.object = slot;
    record.stan = tile;
    record.position = position;
    int i = 0;
    while (rooms[static_cast<size_t>(i)] != kNoRoom && i < 7) {
        record.rooms[static_cast<size_t>(i)] = rooms[static_cast<size_t>(i)];
        i++;
    }
    record.rooms[static_cast<size_t>(i)] = kNoRoom;

    if (seedFromRooms) {
        record.flags |= PropFlag::SeedFromRooms;
    }

    props_->ActivateThisFrame(prop);
    props_->Enable(prop);
    Smoke& smoke = smokes_[static_cast<size_t>(slot)];
    smoke.prop = prop;
    smoke.duration = 0;
    smoke.type = static_cast<int16_t>(smokeType);
}

void Explosions::InitParticle(const Vec3& spawn, float scatter, float horizontalDrift, float verticalDrift, float size,
                              const RandomFn& random) {
    if (particles_.empty()) {
        return;
    }
    const float rand1 = (2.0f * Unit(random)) - 1.0f;
    const float rand2 = (Unit(random) * 1.12f) - 0.12f;
    const float rand3 = (2.0f * Unit(random)) - 1.0f;

    Particle& particle = particles_[nextParticle_];
    particle.active = 1;

    particle.position[0] = spawn[0] + (scatter * rand1);
    particle.position[1] = spawn[1] + (scatter * rand2);
    particle.position[2] = spawn[2] + (scatter * rand3);

    particle.positionDrift[0] = rand1 * horizontalDrift;
    particle.positionDrift[1] = rand2 * verticalDrift;
    particle.positionDrift[2] = rand3 * horizontalDrift;

    // The debris's four corners, each a random 0.75 to 1.5 of the size.
    const auto corner = [&](float sign) {
        return static_cast<int16_t>(static_cast<int32_t>(((Unit(random) * 0.75f) + 0.75f) * (sign * size)));
    };
    particle.vertices[0].position[0] = corner(1.0f);
    particle.vertices[0].position[1] = 0;
    particle.vertices[0].position[2] = corner(1.0f);

    particle.vertices[1].position[0] = corner(1.0f);
    particle.vertices[1].position[1] = 0;
    particle.vertices[1].position[2] = corner(-1.0f);

    particle.vertices[2].position[0] = corner(-1.0f);
    particle.vertices[2].position[1] = 0;
    particle.vertices[2].position[2] = corner(-1.0f);

    particle.vertices[3].position[0] = corner(-1.0f);
    particle.vertices[3].position[1] = 0;
    particle.vertices[3].position[2] = corner(1.0f);

    // Which quarter of the debris image each uses.
    const auto quarterS = static_cast<int16_t>((random() & 3) << 8);
    const auto quarterT = static_cast<int16_t>((random() & 3) << 8);
    particle.vertices[0].texture = { static_cast<int16_t>(quarterS + 0xE0), static_cast<int16_t>(quarterT + 0xE0) };
    particle.vertices[1].texture = { static_cast<int16_t>(quarterS + 0xE0), quarterT };
    particle.vertices[2].texture = { quarterS, quarterT };
    particle.vertices[3].texture = { quarterS, static_cast<int16_t>(quarterT + 0xE0) };

    if (random() & 1) {
        for (EffectVertex& vertex : particle.vertices) {
            const auto shade = static_cast<uint8_t>(0xFF - (random() & 0x3F));
            vertex.colour = { shade, shade, shade, 0xDC };
        }
    } else {
        for (EffectVertex& vertex : particle.vertices) {
            const auto shade = static_cast<uint8_t>(random() & 0x3F);
            vertex.colour = { shade, shade, shade, 0xDC };
        }
    }

    particle.rotation[0] = Unit(random) * kTau;
    particle.rotation[1] = Unit(random) * kTau;
    particle.rotation[2] = Unit(random) * kTau;

    particle.rotationDrift[0] = Unit(random) * 0.1f;
    particle.rotationDrift[1] = Unit(random) * 0.1f;
    particle.rotationDrift[2] = Unit(random) * 0.1f;

    nextParticle_++;
    if (nextParticle_ >= particles_.size()) {
        nextParticle_ = 0;
    }
}

std::vector<int> Explosions::PropRoomIds(const Prop& prop) const {
    std::vector<int> ids;
    if (prop.stan == kNoTile) {
        return ids;
    }
    if (prop.type == PropType::Viewer && prop.object < 0) {
        ids.push_back(stan_->TileRoom(prop.stan));
        return ids;
    }
    for (size_t i = 0; i < prop.rooms.size() && prop.rooms[i] != kNoRoom; i++) {
        ids.push_back(prop.rooms[i]);
    }
    return ids;
}

void Explosions::InflictDamage(int prop, Explosion& explosion, float horizontalRange, float verticalRange,
                               const RandomFn& random) {
    if (explosion.age < explosion.nextDamage) {
        return;
    }
    const ExplosionType& type = ExplosionTypeOf(explosion.type);

    std::vector<int> rooms = PropRoomIds((*props_)[prop]);
    rooms.push_back(-1);
    const std::vector<int16_t> nearby = props_->RoomProps(rooms.data());
    const Vec3 origin = (*props_)[prop].position;

    // The smallest of the three axes' remaining fractions of the range.
    const auto smallestFraction = [&](float xdist, float ydist, float zdist) {
        float xfrac = xdist / horizontalRange;
        float yfrac = ydist / verticalRange;
        float zfrac = zdist / horizontalRange;
        if (xfrac < 0.0f) {
            xfrac = -xfrac;
        }
        if (yfrac < 0.0f) {
            yfrac = -yfrac;
        }
        if (zfrac < 0.0f) {
            zfrac = -zfrac;
        }
        xfrac = 1.0f - xfrac;
        yfrac = 1.0f - yfrac;
        zfrac = 1.0f - zfrac;
        float minfrac = xfrac;
        if (yfrac < minfrac) {
            minfrac = yfrac;
        }
        if (zfrac < minfrac) {
            minfrac = zfrac;
        }
        return minfrac;
    };
    const auto inRange = [&](float xdist, float ydist, float zdist) {
        return xdist <= horizontalRange && -horizontalRange <= xdist && ydist <= verticalRange &&
               -verticalRange <= ydist && zdist <= horizontalRange && -horizontalRange <= zdist;
    };

    for (const int16_t id : nearby) {
        const Prop& other = (*props_)[id];
        if (id == explosion.source || other.timeToRegen != 0) {
            continue;
        }

        if (other.type == PropType::Obj || other.type == PropType::Weapon || other.type == PropType::Door) {
            Vec3 position;
            if (!hooks_.objectPosition || !hooks_.objectPosition(id, &position)) {
                continue;
            }
            const float xdist = position[0] - origin[0];
            const float ydist = position[1] - origin[1];
            const float zdist = position[2] - origin[2];
            if (inRange(xdist, ydist, zdist)) {
                float minfrac = smallestFraction(xdist, ydist, zdist);
                minfrac = minfrac * 1.0f * type.damage;
                if (hooks_.objectDamageable && hooks_.objectDamageable(id)) {
                    hooks_.detonateObject(id, ((Fraction(random) * 0.5f) + 1.0f) * minfrac, position, 0x1D,
                                          explosion.player);
                }
            }
        } else if (other.type == PropType::Chr || other.type == PropType::Viewer) {
            float xdist = other.position[0] - origin[0];
            const float ydist = other.position[1] - origin[1];
            float zdist = other.position[2] - origin[2];
            if (inRange(xdist, ydist, zdist)) {
                float minfrac = smallestFraction(xdist, ydist, zdist);
                minfrac *= minfrac;
                minfrac = minfrac * 1.0f * type.damage;
                if (other.type == PropType::Chr) {
                    if (hooks_.damageChr) {
                        hooks_.damageChr(id, origin, minfrac);
                    }
                } else {
                    if (xdist != 0.0f || zdist != 0.0f) {
                        const float length = std::sqrt((xdist * xdist) + (zdist * zdist));
                        xdist *= 1.0f / length;
                        zdist *= 1.0f / length;
                    }
                    if (hooks_.damagePlayer) {
                        hooks_.damagePlayer(id, minfrac, xdist, zdist, explosion.player);
                    }
                }
            }
        }
    }

    explosion.nextDamage = static_cast<int16_t>(explosion.age + (type.duration >> 2));
}

bool Explosions::TickExplosion(int prop, int32_t clockTimer, const RandomFn& random) {
    Prop& record = (*props_)[prop];
    Explosion& explosion = explosions_[static_cast<size_t>(record.object)];
    const ExplosionType& type = ExplosionTypeOf(explosion.type);

    if (clockTimer == 0) {
        return false;
    }

    const float lvupdate = (clockTimer < 15) ? static_cast<float>(clockTimer) : 15.0f;

    if (explosion.age >= 8 && explosion.age < type.duration) {
        float hrange = type.hrange + (type.hchange * static_cast<float>(explosion.age));
        float vrange = type.vrange + (type.vchange * static_cast<float>(explosion.age));

        if (explosion.type == 0xE) {
            if (explosion.age < 0x20) {
                record.position[1] += 10.0f * lvupdate;
            }
            if (explosion.age >= 0x21) {
                hrange = (static_cast<float>(explosion.age) * 3.0f) + 40.0f;
                if (hrange > 300.0f) {
                    hrange = 300.0f;
                }
                vrange = 20.0f;
            }
        }

        const int spawns =
            static_cast<int32_t>((static_cast<float>(type.propagationRate) * static_cast<float>(explosion.age)) /
                                 static_cast<float>(type.duration)) +
            1;
        for (int n = 0; n < spawns; n++) {
            for (Part& part : explosion.parts) {
                if (part.frame == 0) {
                    part.frame = 1;

                    part.position[0] = record.position[0] + ((Fraction(random) - 0.5f) * hrange);
                    part.position[1] = record.position[1] + ((Fraction(random) - 0.5f) * vrange);
                    part.position[2] = record.position[2] + ((Fraction(random) - 0.5f) * hrange);

                    const float size = ((Fraction(random) * 0.5f) + 1.0f) * type.size;
                    const float angle = Fraction(random) * kTau;

                    part.size = GameCosf(angle) * size;
                    part.rotation = GameSinf(angle) * size;
                    break;
                }
            }
        }

        // explosionGetBboxAtFrame.
        hrange = (hrange * 0.5f) + type.size * 1.5f;
        vrange = (vrange * 0.5f) + type.size * 1.5f;

        const Vec3 low = { record.position[0] - hrange, record.position[1] - vrange, record.position[2] - hrange };
        const Vec3 high = { record.position[0] + hrange, record.position[1] + vrange, record.position[2] + hrange };
        props_->UpdateRoomList(prop, low, high, hrange);

        vrange = type.range + (((type.damageRange - type.range) * static_cast<float>(explosion.age)) /
                               static_cast<float>(type.duration));
        InflictDamage(prop, explosion, vrange, vrange, random);
    }

    for (int k = 0; k < static_cast<int32_t>(lvupdate); k++) {
        explosion.age++;

        for (Part& part : explosion.parts) {
            if (part.frame > 0) {
                part.frame++;
            }
        }

        if ((explosion.age == 0xF && explosion.type == 0xE) ||
            ((explosion.age + 0x14) == type.duration && explosion.type != 0xE)) {
            const bool seed = (record.flags & PropFlag::SeedFromRooms) != 0;
            const Prop* source = explosion.source >= 0 ? &(*props_)[explosion.source] : nullptr;
            if (source != nullptr && source->stan != kNoTile) {
                Vec3 position = source->position;
                if (source->type == PropType::Obj) {
                    Vec3 object;
                    if (hooks_.objectPosition && hooks_.objectPosition(explosion.source, &object)) {
                        position = object;
                    }
                }
                CreateSmoke(position, source->stan, type.smokeType, source->rooms, seed);
            } else {
                CreateSmoke(record.position, record.stan, type.smokeType, record.rooms, seed);
            }
        }

        if (explosion.age == (type.duration >> 1) && explosion.scorches != 0 && impacts_ != nullptr) {
            const Vec3 roomPosition = roomPosition_ ? roomPosition_(explosion.room) : Vec3{};
            impacts_->CreateScorch(explosion.position, type.size * 4.0f, explosion.room, roomPosition, levelScale_,
                                   random);
        }
    }

    if (explosion.age >= type.duration + static_cast<int32_t>(16.0f * type.flareSpeed)) {
        explosion.prop = -1;
        return true;
    }
    return false;
}

bool Explosions::TickSmoke(int prop, int32_t clockTimer, const RandomFn& random) {
    Prop& record = (*props_)[prop];
    Smoke& smoke = smokes_[static_cast<size_t>(record.object)];

    if (clockTimer == 0) {
        return false;
    }
    const SmokeType& type = SmokeTypeOf(smoke.type);

    const float lvupdate = (clockTimer < 15) ? static_cast<float>(clockTimer) : 15.0f;

    for (int i = 0; i < static_cast<int32_t>(lvupdate); i++) {
        smoke.duration += 1;

        for (SmokePart& part : smoke.parts) {
            if (part.size != 0.0f) {
                part.position[1] += 0.3f;
                part.size += 0.15f;

                part.alpha -= type.foregroundRotation;
                part.count++;
                part.rotation += part.deltaRotation;

                part.offset1 += 0.02f + Fraction(random) * 0.01f;
                part.offset2 += 0.02f + Fraction(random) * 0.01f;

                if (part.alpha < 4.0f) {
                    part.size = 0.0f;
                }
            }
        }

        if (smoke.duration < type.duration) {
            if (smoke.duration % type.rateDissolve == 1) {
                for (SmokePart& part : smoke.parts) {
                    if (part.size == 0.0f) {
                        part.size = static_cast<float>(type.size) * (Fraction(random) * 0.5f + 1.0f);
                        part.alpha = static_cast<float>(random() % 70U) + 110.0f;
                        part.count = 0;
                        part.rotation = Fraction(random) * kTau;
                        part.deltaRotation = (0.5f - Fraction(random)) * type.backgroundRotation;

                        part.position = record.position;

                        part.offset1 = Fraction(random) * 0.5f;
                        part.offset2 = Fraction(random) * 0.5f;

                        if (smoke.duration > type.duration - type.propagatedClouds) {
                            part.alpha *= static_cast<float>(type.duration - smoke.duration) /
                                          static_cast<float>(type.propagatedClouds);
                        }
                        break;
                    }
                }
            }
        }
    }

    Vec3 low = { record.position[0] - 1.0f, record.position[1] - 1.0f, record.position[2] - 1.0f };
    Vec3 high = { record.position[0] + 1.0f, record.position[1] + 1.0f, record.position[2] + 1.0f };

    float radius = 0.0f;

    for (const SmokePart& part : smoke.parts) {
        if (part.size != 0.0f) {
            for (size_t axis = 0; axis < 3; axis++) {
                if (low[axis] > part.position[axis] - part.size) {
                    low[axis] = part.position[axis] - part.size;
                } else if (high[axis] < part.position[axis] - part.size) {
                    high[axis] = part.position[axis] - part.size;
                }
            }
        }
    }

    const float xExtent = record.position[0] - low[0];
    if (xExtent > 0.0f) {
        radius = xExtent;
    }
    if (radius < record.position[2] - low[2]) {
        radius = record.position[2] - low[2];
    }
    if (radius < high[0] - record.position[0]) {
        radius = high[0] - record.position[0];
    }
    if (radius < high[2] - record.position[2]) {
        radius = high[2] - record.position[2];
    }

    props_->UpdateRoomList(prop, low, high, radius);

    bool finished = false;
    if (smoke.duration > type.rateDissolve) {
        finished = true;
        for (const SmokePart& part : smoke.parts) {
            if (part.size > 0.0f) {
                finished = false;
                break;
            }
        }
    }

    if (finished) {
        smoke.prop = -1;
        return true;
    }
    return false;
}

void Explosions::UpdateParticles(int32_t clockTimer, const RandomFn& random) {
    const float scalar = clockTimer < 0xF ? static_cast<float>(clockTimer) : 15.0f;

    for (Particle& particle : particles_) {
        if (particle.active <= 0) {
            continue;
        }
        particle.active += static_cast<int32_t>(scalar);

        for (size_t axis = 0; axis < 3; axis++) {
            particle.rotation[axis] += particle.rotationDrift[axis] * scalar;
        }
        particle.position[0] += particle.positionDrift[0] * scalar;
        particle.position[2] += particle.positionDrift[2] * scalar;

        for (int j = 0; j < static_cast<int32_t>(scalar); j++) {
            // Sends the debris flying up at first, then gravity pulls it down.
            particle.position[1] += particle.positionDrift[1];
            if (particle.positionDrift[1] > -3.75f) {
                particle.positionDrift[1] -= 0.2f;
            }
        }

        if (particle.active >= 0x65 && (!(random() & 0x1F) || particle.active == 0x12C)) {
            particle.active = 0;
        }

        if (particle.position[1] < -30000.0f || particle.position[1] > 30000.0f) {
            particle.active = 0;
        }
    }
}

void Explosions::SetDepth(int prop, const Mtxf& worldToScreen) {
    Prop& record = (*props_)[prop];
    record.zDepth = -((((worldToScreen.m[0][2] * record.position[0]) + (worldToScreen.m[1][2] * record.position[1])) +
                       (worldToScreen.m[2][2] * record.position[2])) +
                      worldToScreen.m[3][2]);
    if (record.zDepth < 100.0f) {
        record.zDepth *= 0.5f;
    } else {
        record.zDepth -= 100.0f;
    }
}

} // namespace Frigate::Level
