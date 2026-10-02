#pragma once

#include "level/GunDraw.h"
#include "level/Projectiles.h"

namespace Frigate::Level {

// What the player's guns throw and launch (gun.c): grenades, knives, mines and
// charges, the grenade launcher's rounds, the rocket launcher's rocket.
class GunLauncher final : public ProjectileLaunch {
  public:
    void Attach(Shots* shots, Projectiles* projectiles) {
        shots_ = shots;
        projectiles_ = projectiles;
    }

    // currentPlayerCreateRocket and sub_GAME_7F05FB00.
    void CreateRocket(PlayerWeapons& weapons, Hand hand);
    void FreeRocket(PlayerWeapons& weapons, Hand hand);

    void UpdateAttachedRocket(PlayerWeapons& weapons, Hand hand, const GunFrame& frame) override;
    void Fire(PlayerWeapons& weapons, Hand hand, const GunFrame& frame) override;
    // gunFireTankShell, which the tank's cannon runs from chraiCheckUseHeldItem.
    void FireTankShell(PlayerWeapons& weapons, Hand hand, const GunFrame& frame) {
        FireRocket(weapons, hand, frame);
    }

  private:
    // generate_player_thrown_grenade, _knife and _object.
    void ThrowGrenade(PlayerWeapons& weapons, Hand hand, const GunFrame& frame);
    void ThrowKnife(PlayerWeapons& weapons, Hand hand, const GunFrame& frame);
    void ThrowObject(PlayerWeapons& weapons, Hand hand, const GunFrame& frame);
    // gunSpawnGLGrenade.
    void SpawnGrenadeRound(PlayerWeapons& weapons, Hand hand, const GunFrame& frame);
    // gunFireTankShell, for the rocket launcher.
    void FireRocket(PlayerWeapons& weapons, Hand hand, const GunFrame& frame);

    // sub_GAME_7F057C14: a random speed and spin.
    void RandomThrow(Vec3* speed, Mtxf* spin);
    // The direction, in the world, the hand's shot would go.
    Vec3 AimDirection(Hand hand);
    // The speed of a throw along `direction`: the player's own movement is added
    // while the clock runs.
    Vec3 InheritMovement(const Vec3& speed, const GunFrame& frame) const;
    void InitProjectile(int prop, const Vec3& target, const Mtxf& matrix, const Vec3& velocity, const Mtxf& spin,
                        const GunFrame& frame);
    float RandomFraction();

    Shots* shots_ = nullptr;
    Projectiles* projectiles_ = nullptr;
};

} // namespace Frigate::Level
