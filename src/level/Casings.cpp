#include "level/Casings.h"
#include "game/Sound.h"

namespace Frigate::Level {

namespace {

// The ROM's constants for the ejection and the fall.
constexpr float kSwitchScale = 0.10000001f;
constexpr float kPistolHorizontalSpeed = 0.5333333f;
constexpr float kRifleHorizontalSpeed = 1.4166666f;
constexpr float kRifleVerticalSpeed = 1.6666666f;
constexpr float kRotationScale = 6.2831855f;
// M_PI_F / 8.
constexpr float kRotationOffset = 0.39269909f;
constexpr float kRandomDivisor = 775875.0f;
constexpr float kGravity = 0.2777778f;
// randomGetNext() as a fraction of one.
constexpr float kRandomFraction = 2.3283064e-10f;
// The factor the pistol casing scales its random terms by, and both kinds scale
// their spin by.
constexpr float kRandomScale = 0.0625f;

float Fraction(const Casings::RandomFn& random) {
    return static_cast<float>(random()) * kRandomFraction;
}

} // namespace

void Casings::Clear() {
    sound_ = Game::Sound::Handle{};
    for (Casing& casing : slots_) {
        casing = Casing{};
    }
}

Casing* Casings::Create(const std::string& model, const Mtxf& matrix) {
    for (Casing& casing : slots_) {
        if (casing.InUse()) {
            continue;
        }
        casing.model = model;
        casing.position = { matrix.m[3][0], matrix.m[3][1], matrix.m[3][2] };
        casing.rotation = IdentityMtxf();
        for (int row = 0; row < 3; row++) {
            for (int column = 0; column < 3; column++) {
                casing.rotation.m[row][column] = matrix.m[row][column];
            }
        }
        return &casing;
    }
    return nullptr;
}

void Casings::Eject(const CasingEject& eject, const RandomFn& random) {
    Mtxf matrix = eject.gunMatrix;
    if (eject.ejectOrigin != nullptr) {
        const Vec3 origin = { (*eject.ejectOrigin)[0] * kSwitchScale, (*eject.ejectOrigin)[1] * kSwitchScale,
                              (*eject.ejectOrigin)[2] * kSwitchScale };
        matrix = Multiply(eject.gunMatrix, TranslationMtxf(origin));
    }

    Casing* casing = Create(eject.model, matrix);
    if (casing == nullptr) {
        return;
    }
    casing->floorY = eject.floorY;

    // The casing flies out to the side and up, then is rotated into the world
    // with the gun.
    if (eject.pistol) {
        float rand = Fraction(random);
        casing->velocity[0] = -(((rand * kPistolHorizontalSpeed) * kRandomScale) + kPistolHorizontalSpeed);
        rand = Fraction(random);
        casing->velocity[1] = ((rand * 2.5f) * 0.0625f) + 2.5f;
        casing->velocity[2] = 0.0f;
    } else {
        float rand = Fraction(random);
        casing->velocity[0] = -(((rand * kRifleHorizontalSpeed) * 0.125f) + kRifleHorizontalSpeed);
        rand = Fraction(random);
        casing->velocity[1] = ((rand * kRifleVerticalSpeed) * 0.125f) + kRifleVerticalSpeed;
        casing->velocity[2] = 0.0f;
    }
    casing->velocity = RotateVector(eject.gunMatrix, casing->velocity);

    Vec3 angles{};
    for (size_t axis = 0; axis < 3; axis++) {
        const float rand = Fraction(random);
        angles[axis] = (((rand + rand) * kRotationScale) * kRandomScale) - kRotationOffset;
    }
    casing->spin = RotationXYZ(angles);

    // The casing has already flown for a random part of a tick.
    uint32_t limit = (random() >> 24) * 0x158679u;
    limit = static_cast<uint32_t>(static_cast<int32_t>(limit) >> 10) + 0x158679u;
    const uint32_t value = random();
    const float elapsed = static_cast<float>(value % limit) / kRandomDivisor;
    const float oldVelocityY = casing->velocity[1];
    const float newVelocityY = oldVelocityY - (elapsed * kGravity);
    casing->velocity[1] = newVelocityY;
    casing->position[1] += (elapsed * (oldVelocityY + newVelocityY)) * 0.5f;
    casing->position[0] += elapsed * casing->velocity[0];
    casing->position[2] += elapsed * casing->velocity[2];

    // The gun's own movement carries it.
    if (eject.clockTimer > 0) {
        for (size_t axis = 0; axis < 3; axis++) {
            casing->velocity[axis] +=
                (eject.gunMatrix.m[3][axis] - eject.previousGunMatrix.m[3][axis]) / eject.globalTimerDelta;
        }
    }
}

void Casings::Update(int32_t clockTimer, float globalTimerDelta) {
    for (Casing& casing : slots_) {
        if (!casing.InUse()) {
            continue;
        }
        const float delta = globalTimerDelta;
        const float newVelocityY = casing.velocity[1] - (delta * kGravity);
        casing.position[1] += delta * 0.5f * (casing.velocity[1] + newVelocityY);

        if (casing.position[1] < casing.floorY) {
            if (sound_.IsNull() && !(handFiring_ && handFiring_())) {
                Game::Sound::PlaySfx(AudioDriver::kSfxCartSpent, sound_.Pending());
            }
            casing = Casing{};
            continue;
        }

        casing.velocity[1] = newVelocityY;
        casing.position[0] += delta * casing.velocity[0];
        casing.position[2] += delta * casing.velocity[2];
        for (int32_t i = 0; i < clockTimer; i++) {
            casing.rotation = MultiplyHomogeneous(casing.spin, casing.rotation);
        }
    }
}

} // namespace Frigate::Level
