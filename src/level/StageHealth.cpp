#include <cmath>

#include "level/StageAi.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/type/Blob.h"
#include "model/Random.h"

namespace Frigate::Level {

namespace {

// EXPLOSION_DEF_PLAYER, and PLAYER_TICKEXPLODE_FACTOR (NTSC).
constexpr int kExplosionPlayer = 18;
constexpr int kTickExplodeFactor = 15;

// RANDOMGETNEXT_F32.
float RandomUnit() {
    return static_cast<float>(RandomGetNext()) * (1.0f / 4294967296.0f);
}

// PLAYERFLAG_NOTIMER, which a hit hides the messages and upper text with.
constexpr int32_t kHitHidesMessages = 4;
// countdownTimerSetVisible's bit for a hit.
constexpr int kCountdownDamageBit = 8;

} // namespace

HealthFrame Stage::MakeHealthFrame() const {
    HealthFrame frame;
    frame.watchState = watch_.State();
    frame.clockTimer = clock_.clockTimer;
    frame.globalTimerDelta = clock_.globalTimerDelta;
    frame.speedFrames = speedFrames_;
    // TODO(https://github.com/liz-baker/frigate/issues/148): g_PlayerIsInTank.
    frame.inTank = false;
    frame.invincible = playerInvincible_;
    frame.handicap = stats_[0].handicap;
    return frame;
}

void Stage::RecordPlayerDamage(float damage, float vectorX, float vectorZ, bool affectsArmour) {
    // bondviewCallRecordDamageKills and record_damage_kills: an open watch
    // redraws its gauges from the values before the hit.
    watch_.RedrawGauges();
    const DamageEffects effects = health_.RecordDamage(MakeHealthFrame(), damage, affectsArmour);
    // TODO(https://github.com/liz-baker/frigate/issues/41): BOND_GET_HIT1_SFX.

    if (effects.killed) {
        KillPlayer();
    }
    if (effects.push) {
        player_.AddShotSpeed(2.0f * vectorX, 2.0f * vectorZ);
    }
}

void Stage::KillPlayer() {
    // bondviewKillCurrentPlayer.
    if (health_.IsDead()) {
        return;
    }
    if (watch_.State() != WatchState::Closed) {
        watch_.Trigger(true);
    }

    bondKilledInAction_ = true;
    health_.Kill();
    player_.BeginDeath();

    // TODO(https://github.com/liz-baker/frigate/issues/148): g_ExplodeTankOnDeathFlag.

    if (weapons_.Ready()) {
        weapons_.EquipWeapon(HandLeft, kItemUnarmed);
        weapons_.EquipWeapon(HandRight, kItemUnarmed);
    }

    PlayerStats& stats = stats_[0];
    if (clock_.stageTicks - lifeStartTicks_ < stats.shortestInning) {
        stats.shortestInning = clock_.stageTicks - lifeStartTicks_;
    }
    lifeStartTicks_ = clock_.stageTicks;
}

void Stage::TickPlayerDamage() {
    // bondviewPlayerTickDamageAndHealth.
    const DamageTickEffects effects = health_.Tick(MakeHealthFrame());

    if (effects.begin) {
        hud_.SetAmmoVisible(AmmoReason::Damage, false);
        hud_.SetSightVisible(SightReason::Damage, false);
        hud_.MessagesOff(kHitHidesMessages);
        hud_.SetUpperTextFlag(HudFlag::NoTimer);
        countdownHidden_ |= kCountdownDamageBit;
    }
    if (effects.fade) {
        colour_.SetColour(effects.red, effects.green, effects.blue, effects.fadeFrac);
    }
    if (effects.clearFade) {
        colour_.SetColour(0xFF, 0xFF, 0xFF, 0.0f);
    }
    if (effects.end) {
        hud_.SetAmmoVisible(AmmoReason::Damage, true);
        hud_.SetSightVisible(SightReason::Damage, true);
        hud_.MessagesOn(kHitHidesMessages);
        hud_.ClearUpperTextFlag(HudFlag::NoTimer);
        countdownHidden_ &= ~kCountdownDamageBit;
    }

    HealthDisplay& display = hud_.Health();
    display.health = health_.ApparentHealth();
    display.armour = health_.ApparentArmour();
    display.showTime = health_.HealthShowTime();
}

void Stage::TickPlayerDeath(const std::shared_ptr<Ship::ResourceManager>& resourceManager) {
    bloodOverlay_ = false;
    if (!health_.IsDead()) {
        return;
    }

    if (!health_.DeathAnimFinished()) {
        if (health_.Dead() == 1) {
            health_.SetDead(2);
            if (!blood_) {
                if (const auto stream = resourceManager->LoadResource<Ship::Blob>("eyeintro/blood")) {
                    blood_.emplace(stream->Data);
                }
            }
            if (blood_) {
                blood_->Start();
            }
            // TODO(https://github.com/liz-baker/frigate/issues/41): set_missionstate(0), the
            // music slots' fade and musicTrack1Play(27).
        } else if (health_.RedBloodFinished()) {
            colour_.SetColour(150, 0, 0, 0.7058824f);
        } else {
            if (clock_.clockTimer > 0 && blood_ && blood_->Advance()) {
                health_.SetRedBloodFinished();
            }
            bloodOverlay_ = blood_.has_value();
        }
    }

    if (player_.DeathAnimationFinished() && health_.RedBloodFinished()) {
        if (!health_.DeathAnimFinished()) {
            health_.SetDeathAnimFinished();
            colour_.Adjust(60.0f, 0, 0, 0, 1.0f);
            colour_.StartBodyFade(120.0f, 0.0f, bodyFadeAlpha_);
        }
        if (colour_.IsComplete()) {
            // TODO(https://github.com/liz-baker/frigate/issues/136): bondviewSetCameraMode
            // (CAMERAMODE_DEATH_CAM_SP) and the killed-in-action exit.
        }
    }
}

void Stage::TickPlayerExplode() {
    // bondviewPlayerTickExplode.
    tickExplodePosition_++;

    if (surroundBondWithExplosions_ && !playerInvincible_ && surroundTicks_ < clock_.globalTimer) {
        const Prop& player = objects_.Props()[player_.PropId()];
        Vec3 position = player.position;

        switch (tickExplodePosition_ % 4) {
            case 0:
                position[0] += 250.0f + 150.0f * RandomUnit();
                break;
            case 1:
                position[0] -= 250.0f + 150.0f * RandomUnit();
                break;
            case 2:
                position[2] += 250.0f + 150.0f * RandomUnit();
                break;
            case 3:
                position[2] -= 250.0f + 150.0f * RandomUnit();
                break;
        }

        position[1] += 200.0f * RandomUnit() - 100.0f;

        explosions_.Create(-1, position, player.stan, kExplosionPlayer, 0, 0, player.rooms, false, RandomGetNext);

        surroundTicks_ =
            (RandomGetNext() % static_cast<uint32_t>(kTickExplodeFactor)) + clock_.globalTimer + kTickExplodeFactor;
    }
}

void Stage::AiBridge::ExplodeAroundBond() {
    // SurroundWithExplosions(0).
    stage_.surroundBondWithExplosions_ = true;
    stage_.surroundTicks_ = 0 + stage_.clock_.globalTimer;
    stage_.tickExplodePosition_ = 0;
}

} // namespace Frigate::Level
