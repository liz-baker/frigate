#include <cmath>

#include "game/Sound.h"
#include "level/StageAi.h"
#include "platform/Rumble.h"
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
    frame.watchState = Cur().watch.State();
    frame.clockTimer = clock_.clockTimer;
    frame.globalTimerDelta = clock_.globalTimerDelta;
    frame.speedFrames = speedFrames_;
    frame.inTank = Cur().move.InTank();
    frame.invincible = playerInvincible_ || cheatInvincible_;
    frame.handicap = CurStats().handicap;
    return frame;
}

void Stage::RecordPlayerDamage(float damage, float vectorX, float vectorZ, bool affectsArmour) {
    // bondviewCallRecordDamageKills and record_damage_kills: an open watch
    // redraws its gauges from the values before the hit.
    // TODO(https://github.com/liz-baker/frigate/issues/387): two or more
    // players also count the hit from behind, drop the inventory and credit
    // the kill, and the scenarios (https://github.com/liz-baker/frigate/issues/389)
    // and a finished round (https://github.com/liz-baker/frigate/issues/388)
    // change how much damage lands.
    Cur().watch.RedrawGauges();
    if (!Cur().health.IsDead() && !cheatInvincible_) {
        RumblePlayer(0.25f);
    }
    const DamageEffects effects = Cur().health.RecordDamage(MakeHealthFrame(), damage, affectsArmour);

    if (effects.killed) {
        KillPlayer();
    }
    if (effects.push) {
        Cur().move.AddShotSpeed(2.0f * vectorX, 2.0f * vectorZ);
        // The hit that pushes the player is the one that registers, and sounds.
        Game::Sound::PlaySfx(AudioDriver::kSfxBondGetHit1);
    }
}

void Stage::RumblePlayer(float duration) {
    if (demoPlayback_) {
        return;
    }
    Platform::Input::Rumble& rumble = Platform::Input::GlobalRumble();
    rumble.Start(current_, duration);
    if (static_cast<int32_t>(Cur().options.controlType) >= 4) {
        rumble.Start(current_ + playerCount_, duration);
    }
}

void Stage::KillPlayer() {
    // bondviewKillCurrentPlayer.
    if (cheatInvincible_ || Cur().health.IsDead()) {
        return;
    }
    if (Cur().watch.State() != WatchState::Closed) {
        Cur().watch.Trigger(true);
    }

    bondKilledInAction_ = true;
    Cur().health.Kill();
    Cur().move.BeginDeath();

    if (Cur().weapons.Ready()) {
        Cur().weapons.EquipWeapon(HandLeft, kItemUnarmed);
        Cur().weapons.EquipWeapon(HandRight, kItemUnarmed);
    }

    PlayerStats& stats = CurStats();
    if (clock_.stageTicks - Cur().lifeStartTicks < stats.shortestInning) {
        stats.shortestInning = clock_.stageTicks - Cur().lifeStartTicks;
    }
    Cur().lifeStartTicks = clock_.stageTicks;
}

void Stage::TickPlayerDamage() {
    // bondviewPlayerTickDamageAndHealth.
    const DamageTickEffects effects = Cur().health.Tick(MakeHealthFrame());

    if (effects.begin) {
        Cur().hud.SetAmmoVisible(AmmoReason::Damage, false);
        Cur().hud.SetSightVisible(SightReason::Damage, false);
        Cur().hud.MessagesOff(kHitHidesMessages);
        Cur().hud.SetUpperTextFlag(HudFlag::NoTimer);
        countdownHidden_ |= kCountdownDamageBit;
    }
    if (effects.fade) {
        Cur().colour.SetColour(effects.red, effects.green, effects.blue, effects.fadeFrac);
    }
    if (effects.clearFade) {
        Cur().colour.SetColour(0xFF, 0xFF, 0xFF, 0.0f);
    }
    if (effects.end) {
        Cur().hud.SetAmmoVisible(AmmoReason::Damage, true);
        Cur().hud.SetSightVisible(SightReason::Damage, true);
        Cur().hud.MessagesOn(kHitHidesMessages);
        Cur().hud.ClearUpperTextFlag(HudFlag::NoTimer);
        countdownHidden_ &= ~kCountdownDamageBit;
    }

    HealthDisplay& display = Cur().hud.Health();
    display.health = Cur().health.ApparentHealth();
    display.armour = Cur().health.ApparentArmour();
    display.showTime = Cur().health.HealthShowTime();
}

void Stage::TickPlayerDeath(const std::shared_ptr<Ship::ResourceManager>& resourceManager) {
    Cur().bloodOverlay = false;
    if (!Cur().health.IsDead()) {
        return;
    }

    if (!Cur().health.DeathAnimFinished()) {
        if (Cur().health.Dead() == 1) {
            Cur().health.SetDead(2);
            if (!Cur().blood) {
                if (const auto stream = resourceManager->LoadResource<Ship::Blob>("eyeintro/blood")) {
                    Cur().blood.emplace(stream->Data);
                }
            }
            if (Cur().blood) {
                Cur().blood->Start();
            }
            if (playerCount_ == 1) {
                // The AI's music slots stop, the mission music with them, and the
                // death theme plays.
                music_.StopSlot(-1);
                music_.Stop();
                Game::Sound::MusicTrackApplySeqpVol(0, Game::Sound::GetMTrack2Vol());
                Game::Sound::SetMusicTrackFade(0, AudioDriver::kMusicFadeUnset);
                Game::Sound::MusicTrackApplySeqpVol(1, 0);
                Game::Sound::SetMusicTrackFade(1, AudioDriver::kMusicFadeUnset);
                Game::Sound::MusicTrackPlay(0, AudioDriver::kMusicDeathSolo);
            }
            // TODO(https://github.com/liz-baker/frigate/issues/388): two or more
            // players set the mission state to 6 here.
        } else if (Cur().health.RedBloodFinished()) {
            Cur().colour.SetColour(150, 0, 0, 0.7058824f);
        } else {
            if (clock_.clockTimer > 0 && Cur().blood && Cur().blood->Advance()) {
                Cur().health.SetRedBloodFinished();
            }
            Cur().bloodOverlay = Cur().blood.has_value();
        }
    }

    if (Cur().move.DeathAnimationFinished() && Cur().health.RedBloodFinished()) {
        if (!Cur().health.DeathAnimFinished()) {
            Cur().health.SetDeathAnimFinished();
            Cur().colour.Adjust(60.0f, 0, 0, 0, 1.0f);
            StartBodyFade(120.0f, 0.0f);
        }
        if (Cur().colour.IsComplete()) {
            if (playerCount_ == 1) {
                EnterDeathCamera();
            }
            // TODO(https://github.com/liz-baker/frigate/issues/387): two or more
            // players respawn on a button press (mp_respawn_handler).
        }
    }
}

void Stage::TickPlayerExplode() {
    // bondviewPlayerTickExplode.
    tickExplodePosition_++;

    if (surroundBondWithExplosions_ && !playerInvincible_ && surroundTicks_ < clock_.globalTimer) {
        const Prop& player = objects_.Props()[Cur().move.PropId()];
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
