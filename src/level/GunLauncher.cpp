#include "level/GunLauncher.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

#include "game/Sound.h"
#include "gfx/Gu.h"
#include "level/Explosions.h"
#include "level/LevelModels.h"
#include "level/WeaponTable.h"

namespace Frigate::Level {

namespace {

constexpr float kFraction = 2.3283064e-10f;
constexpr float kPi = 3.1415927f;

// PROP_CHR* model numbers.
constexpr int kModelKnife = 186;
constexpr int kModelGrenade = 196;
constexpr int kModelRemoteMine = 199;
constexpr int kModelProximityMine = 200;
constexpr int kModelTimedMine = 201;
constexpr int kModelRocket = 202;
constexpr int kModelGrenadeRound = 203;
constexpr int kModelBombCase = 226;
constexpr int kModelBug = 245;
constexpr int kModelMicroCamera = 246;
constexpr int kModelGoldenEyeKey = 248;
constexpr int kModelPlastique = 273;

// THROWN_ITEM_REFRESH_RATE, THROWN_ITEM_TIMER_SOLO, THROWN_ITEM_TIMER_DEFAULT and
// GLGRENADE_TIMER, in their NTSC values.
constexpr int kRefreshRate = 60;
constexpr int kTimerDefault = 240;
constexpr int kGrenadeRoundTimer = 1200;
// g_GLGrenadeLaunchUnk8C and g_GLGrenadeLaunchUnk94.
constexpr float kRoundBounce = 0.3f;
constexpr float kRoundFriction = 0.13333333f;

Mat4 ToMat4(const Mtxf& matrix) {
    Mat4 out{};
    std::memcpy(out.m, matrix.m, sizeof(out.m));
    return out;
}

Mtxf WithoutTranslation(Mtxf matrix) {
    matrix.m[3][0] = 0.0f;
    matrix.m[3][1] = 0.0f;
    matrix.m[3][2] = 0.0f;
    return matrix;
}

void SetOwner(PlacedObject* object, int player) {
    object->runtimeFlags &= ~ObjectRuntime::OwnerMask;
    object->runtimeFlags |= static_cast<uint32_t>(player) << ObjectRuntime::OwnerShift;
}

} // namespace

float GunLauncher::RandomFraction() {
    return static_cast<float>(shots_->Scene().random()) * kFraction;
}

void GunLauncher::RandomThrow(Vec3* speed, Mtxf* spin) {
    // sub_GAME_7F057C14.
    (*speed)[0] = RandomFraction() * 1.6666666f * 4.0f - 3.3333333f;
    (*speed)[1] = RandomFraction() * 1.6666666f * 4.0f;
    (*speed)[2] = RandomFraction() * 1.6666666f * 4.0f - 3.3333333f;
    Vec3 angles{};
    for (float& angle : angles) {
        // mtxLoadRandomRotation, with the US game's full-turn scale.
        angle = RandomFraction() * 6.2831855f * 0.03125f - 0.09817477f;
    }
    *spin = RotationXYZ(angles);
}

Vec3 GunLauncher::AimDirection(Hand hand) {
    Vec3 origin{};
    Vec3 direction{};
    shots_->BulletPath(hand, &origin, &direction);
    return RotateVector(shots_->Scene().camera->ViewToWorld(), direction);
}

Vec3 GunLauncher::InheritMovement(const Vec3& speed, const GunFrame& frame) const {
    Vec3 out = speed;
    if (frame.clockTimer > 0) {
        const ShotScene& scene = shots_->Scene();
        const Prop& player = (*scene.props)[scene.playerProp()];
        for (size_t axis = 0; axis < 3; axis++) {
            out[axis] += (player.position[axis] - frame.previousPosition[axis]) / frame.globalTimerDelta;
        }
    }
    return out;
}

void GunLauncher::InitProjectile(int prop, const Vec3& target, const Mtxf& matrix, const Vec3& velocity,
                                 const Mtxf& spin, const GunFrame& frame) {
    // gunInitProjectileFromPlayer.
    const ShotScene& scene = shots_->Scene();
    const int playerProp = scene.playerProp();
    const Prop& player = (*scene.props)[playerProp];
    const int room = player.stan != kNoTile ? scene.stan->TileRoom(player.stan) : 0;
    projectiles_->InitFromPlayer(prop, target, matrix, velocity, spin, playerProp, frame.floorHeight,
                                 scene.camera->WorldPosition(), room);
}

// -- The rocket launcher's own rocket -----------------------------------------------------

void GunLauncher::CreateRocket(PlayerWeapons& weapons, Hand handNum) {
    // currentPlayerCreateRocket.
    HandState& hand = weapons.MutableHand(handNum);
    if (hand.rocketProp >= 0 || hand.ammoInMagazine <= 0) {
        return;
    }
    Objects& objects = *shots_->Scene().objects;
    const int prop = objects.CreateItem(kModelRocket, kItemRocketRound);
    if (prop < 0) {
        return;
    }
    hand.rocketProp = prop;
    hand.firedRocket = false;
    objects.ForProp(prop)->weaponTimer = 1;
}

void GunLauncher::FreeRocket(PlayerWeapons& weapons, Hand handNum) {
    // sub_GAME_7F05FB00.
    HandState& hand = weapons.MutableHand(handNum);
    if (hand.rocketProp >= 0) {
        shots_->Scene().objects->Free(hand.rocketProp);
    }
}

void GunLauncher::UpdateAttachedRocket(PlayerWeapons& weapons, Hand handNum, const GunFrame& frame) {
    // gunUpdateAttachedRocket.
    HandState& hand = weapons.MutableHand(handNum);
    if (hand.rocketProp < 0) {
        return;
    }
    const ShotScene& scene = shots_->Scene();
    PlacedObject* rocket = scene.objects->ForProp(hand.rocketProp);
    if (rocket == nullptr) {
        return;
    }
    const Prop& player = (*scene.props)[scene.playerProp()];

    Mtxf world = WithoutTranslation(hand.worldMatrix);
    ScaleRows(&world, rocket->scale);
    scene.objects->SetPosition(rocket, hand.muzzlePosition, world, player.stan);
    scene.objects->UpdateCollision(rocket);

    Mtxf placed = rocket->mtx;
    for (size_t axis = 0; axis < 3; axis++) {
        placed.m[3][axis] = rocket->runtimePosition[axis];
    }
    const Mtxf view = MultiplyHomogeneous(frame.worldToView, placed);
    if (rocket->instance) {
        hand.rocketModel = static_cast<LevelModel&>(*rocket->instance).Instance();
        hand.rocketModel->SetRootOrigin({ 0.0f, 0.0f, 0.0f });
        hand.rocketModel->Pose(ToMat4(view));
    }
    Prop& record = (*scene.props)[hand.rocketProp];
    record.flags |= PropFlag::OnScreen;
    record.zDepth = -view.m[3][2];
}

// -- Firing ---------------------------------------------------------------------------------

void GunLauncher::Fire(PlayerWeapons& weapons, Hand hand, const GunFrame& frame) {
    const int32_t item = weapons.WeaponId(hand);
    if (item == kItemGrenadeLauncher || item == kItemFlarePistol || item == kItemPitonGun) {
        SpawnGrenadeRound(weapons, hand, frame);
    } else if (item == kItemGrenade) {
        ThrowGrenade(weapons, hand, frame);
    } else if (item == kItemRocketLauncher) {
        FireRocket(weapons, hand, frame);
    } else if (item == kItemThrowKnife) {
        ThrowKnife(weapons, hand, frame);
    } else if (item == kItemRemoteMine || item == kItemProximityMine || item == kItemTimedMine ||
               item == kItemBombCase || item == kItemBug || item == kItemMicroCamera || item == kItemGoldenEyeKey ||
               item == kItemPlastique) {
        ThrowObject(weapons, hand, frame);
    }
}

void GunLauncher::ThrowGrenade(PlayerWeapons& weapons, Hand handNum, const GunFrame& frame) {
    // generate_player_thrown_grenade.
    const HandState& hand = weapons.Hands(handNum);
    const int32_t item = weapons.WeaponId(handNum);

    Vec3 speed{};
    Mtxf spin;
    RandomThrow(&speed, &spin);
    const Vec3 direction = AimDirection(handNum);
    speed = { direction[0] * 16.666666f, (direction[1] * 16.666666f) + 5.0f, direction[2] * 16.666666f };
    speed = InheritMovement(speed, frame);

    const Vec3 origin = { hand.worldMatrix.m[3][0], hand.worldMatrix.m[3][1], hand.worldMatrix.m[3][2] };
    const Mtxf matrix = Multiply(WithoutTranslation(hand.worldMatrix), IdentityMtxf());

    const int prop = shots_->Scene().objects->CreateItem(kModelGrenade, item);
    PlacedObject* grenade = shots_->Scene().objects->ForProp(prop);
    if (grenade == nullptr) {
        return;
    }
    grenade->weaponTimer = static_cast<int16_t>(std::max(0, kTimerDefault - weapons.LastZTriggerTimer()));
    SetOwner(grenade, 0);
    InitProjectile(prop, origin, matrix, speed, spin, frame);

    if ((grenade->runtimeFlags & ObjectRuntime::HasProjectile) != 0 && grenade->projectile >= 0) {
        Projectile& projectile = shots_->Scene().objects->ProjectileAt(grenade->projectile);
        projectile.flags |= ProjectileFlag::Bouncy;
        projectile.bounce = kRoundBounce;
        projectile.friction = kRoundFriction;
        projectile.refreshRate = kRefreshRate;
        // The throw's sound is heard from where the object is.
        Game::Sound::PostVolumeAtPositionDefault(Game::Sound::PlaySfx(AudioDriver::kSfxGrenadeThrow),
                                                 grenade->runtimePosition);
    }
}

void GunLauncher::ThrowKnife(PlayerWeapons& weapons, Hand handNum, const GunFrame& frame) {
    // generate_player_thrown_knife.
    const HandState& hand = weapons.Hands(handNum);

    Vec3 speed{};
    Mtxf spin;
    RandomThrow(&speed, &spin);
    const Vec3 direction = AimDirection(handNum);
    speed = { direction[0] * 25.0f, (direction[1] * 25.0f) + 5.0f, direction[2] * 25.0f };
    speed = InheritMovement(speed, frame);

    const Vec3 origin = { hand.worldMatrix.m[3][0], hand.worldMatrix.m[3][1], hand.worldMatrix.m[3][2] };
    Mtxf matrix = RotationZ(4.712389f);
    matrix = Multiply(RotationX(kPi), matrix);
    matrix = Multiply(WithoutTranslation(hand.worldMatrix), matrix);

    // The knife turns end over end about its own up axis.
    Gu::MatrixF rotation;
    const float turns = static_cast<float>(shots_->Scene().random()) * (0.5f / static_cast<float>(INT_MAX));
    Gu::RotateF(rotation, 360.0f / (turns + 12.1f), matrix.m[1][0], matrix.m[1][1], matrix.m[1][2]);
    std::memcpy(spin.m, rotation, sizeof(spin.m));

    Objects& objects = *shots_->Scene().objects;
    const int prop = objects.CreateItem(kModelKnife, kItemThrowKnife);
    PlacedObject* knife = objects.ForProp(prop);
    if (knife == nullptr) {
        return;
    }
    SetOwner(knife, 0);
    InitProjectile(prop, origin, matrix, speed, spin, frame);

    if ((knife->runtimeFlags & ObjectRuntime::HasProjectile) != 0 && knife->projectile >= 0) {
        Projectile& projectile = objects.ProjectileAt(knife->projectile);
        projectile.flags |= ProjectileFlag::Bouncy;
        projectile.bounce = 0.1f;
        projectile.refreshRate = kRefreshRate;
        knife->runtimeFlags |= ObjectRuntime::ThrowingKnife;
    }
    projectiles_->UpdateKnifeSound(*knife);
}

void GunLauncher::ThrowObject(PlayerWeapons& weapons, Hand handNum, const GunFrame& frame) {
    // generate_player_thrown_object.
    const HandState& hand = weapons.Hands(handNum);
    const int32_t item = weapons.WeaponId(handNum);
    const float baseVelocity = item == kItemGoldenEyeKey ? 6.6666665f : 16.666666f;

    Vec3 speed{};
    Mtxf spin;
    RandomThrow(&speed, &spin);
    const Vec3 direction = AimDirection(handNum);
    speed = { direction[0] * baseVelocity, (direction[1] * baseVelocity) + 5.0f, direction[2] * baseVelocity };
    speed = InheritMovement(speed, frame);

    const Vec3 origin = { hand.worldMatrix.m[3][0], hand.worldMatrix.m[3][1], hand.worldMatrix.m[3][2] };
    const Mtxf matrix = Multiply(WithoutTranslation(hand.worldMatrix), IdentityMtxf());

    // A thrown GoldenEye key is the one the player kept, if there is one.
    const ShotScene& scene = shots_->Scene();
    int kept = -1;
    if (item == kItemGoldenEyeKey) {
        if (scene.takeKeptWeapon) {
            kept = scene.takeKeptWeapon(item);
        }
        weapons.EquipPrevious();
    }
    int model = kModelRemoteMine;
    switch (item) {
        case kItemProximityMine:
            model = kModelProximityMine;
            break;
        case kItemTimedMine:
            model = kModelTimedMine;
            break;
        case kItemBombCase:
            model = kModelBombCase;
            break;
        case kItemBug:
            model = kModelBug;
            break;
        case kItemMicroCamera:
            model = kModelMicroCamera;
            break;
        case kItemGoldenEyeKey:
            model = kModelGoldenEyeKey;
            break;
        case kItemPlastique:
            model = kModelPlastique;
            break;
        default:
            break;
    }

    Objects& objects = *scene.objects;
    const int prop = kept >= 0 ? kept : objects.CreateItem(model, item);
    PlacedObject* thrown = objects.ForProp(prop);
    if (thrown == nullptr) {
        return;
    }
    thrown->weaponTimer = static_cast<int16_t>(ThrownItemTimer(item, weapons.PlayerCount()));
    SetOwner(thrown, 0);
    InitProjectile(prop, origin, matrix, speed, spin, frame);

    if ((thrown->runtimeFlags & ObjectRuntime::HasProjectile) != 0 && thrown->projectile >= 0) {
        Projectile& projectile = objects.ProjectileAt(thrown->projectile);
        projectile.flags |= ProjectileFlag::Bouncy;
        projectile.bounce = 0.1f;
        projectile.refreshRate = kRefreshRate;
        // The throw's sound is heard from where the object is.
        Game::Sound::PostVolumeAtPositionDefault(Game::Sound::PlaySfx(AudioDriver::kSfxGrenadeThrow),
                                                 thrown->runtimePosition);
    }
}

void GunLauncher::SpawnGrenadeRound(PlayerWeapons& weapons, Hand handNum, const GunFrame& frame) {
    // gunSpawnGLGrenade.
    const HandState& hand = weapons.Hands(handNum);
    const Vec3 direction = AimDirection(handNum);
    Vec3 velocity = { direction[0] * 33.333332f, direction[1] * 33.333332f, direction[2] * 33.333332f };
    velocity = InheritMovement(velocity, frame);
    const Mtxf matrix = WithoutTranslation(hand.worldMatrix);

    Objects& objects = *shots_->Scene().objects;
    const int prop = objects.CreateItem(kModelGrenadeRound, kItemGrenadeRound);
    PlacedObject* round = objects.ForProp(prop);
    if (round == nullptr) {
        return;
    }
    round->weaponTimer = kGrenadeRoundTimer;
    SetOwner(round, 0);
    InitProjectile(prop, hand.muzzlePosition, matrix, velocity, IdentityMtxf(), frame);

    if ((round->runtimeFlags & ObjectRuntime::HasProjectile) != 0 && round->projectile >= 0) {
        Projectile& projectile = objects.ProjectileAt(round->projectile);
        projectile.bounce = kRoundBounce;
        projectile.friction = kRoundFriction;
        projectile.refreshRate = kRefreshRate;
    }
}

void GunLauncher::FireRocket(PlayerWeapons& weapons, Hand handNum, const GunFrame& frame) {
    // gunFireTankShell, for the rocket launcher.
    HandState& hand = weapons.MutableHand(handNum);
    const int32_t item = weapons.WeaponId(handNum);
    const bool tankShell = item == kItemTankShells;

    Vec3 direction{};
    Vec3 spawn{};
    Vec3 unscaled{};
    Vec3 velocity{};
    if (tankShell) {
        if (frame.tankAimed) {
            direction = frame.tankAim;
            spawn = frame.tankMuzzle;
        } else {
            // sub_GAME_7F068190: the sight's direction, with no spread.
            direction = RotateVector(shots_->Scene().camera->ViewToWorld(), shots_->SightDirection());
            spawn = (*shots_->Scene().props)[shots_->Scene().playerProp()].position;
        }
        velocity = { direction[0] * kTankShellSpeed, direction[1] * kTankShellSpeed, direction[2] * kTankShellSpeed };
        velocity = InheritMovement(velocity, frame);
        shots_->Scene().explosions->SetSixEntries();
    } else {
        direction = AimDirection(handNum);
        spawn = hand.muzzlePosition;
        // D_80053DDC.
        unscaled = { direction[0] * 1.111111f, direction[1] * 1.111111f, direction[2] * 1.111111f };
        velocity = { unscaled[0] * frame.globalTimerDelta, unscaled[1] * frame.globalTimerDelta,
                     unscaled[2] * frame.globalTimerDelta };
        velocity = InheritMovement(velocity, frame);
    }
    const Mtxf matrix = WithoutTranslation(hand.worldMatrix);

    Objects& objects = *shots_->Scene().objects;
    int prop = -1;
    if (hand.rocketProp >= 0) {
        prop = hand.rocketProp;
        hand.firedRocket = true;
    } else {
        prop = objects.CreateItem(kModelRocket, kItemRocketRound);
    }
    PlacedObject* rocket = objects.ForProp(prop);
    if (rocket == nullptr) {
        return;
    }
    rocket->weaponTimer = -1;
    SetOwner(rocket, 0);
    InitProjectile(prop, spawn, matrix, velocity, IdentityMtxf(), frame);

    if ((rocket->runtimeFlags & ObjectRuntime::HasProjectile) != 0 && rocket->projectile >= 0) {
        Projectile& projectile = objects.ProjectileAt(rocket->projectile);
        projectile.flags |= ProjectileFlag::Launching;
        if (!tankShell) {
            projectile.flags |= ProjectileFlag::Rising;
            projectile.apexHeight = rocket->runtimePosition[1];
            projectile.apexSpeed = projectile.speed[1];
            projectile.acceleration = unscaled;
            projectile.refreshRate = kRefreshRate;
            // The flight sound plays on whichever of the projectile's two slots is free.
            if (projectile.sounds[0].IsNull()) {
                Game::Sound::PlaySfx(AudioDriver::kSfxRocketLaunch, projectile.sounds[0].Pending());
            } else if (projectile.sounds[1].IsNull()) {
                Game::Sound::PlaySfx(AudioDriver::kSfxRocketLaunch, projectile.sounds[1].Pending());
            }
        }
    }
}

} // namespace Frigate::Level
