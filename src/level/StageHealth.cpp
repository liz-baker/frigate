#include <cmath>

#include "model/GameMath.h"

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
    frame.multiplayer = playerCount_ >= 2;
    return frame;
}

void Stage::DamagePlayer(int victim, int attacker, float damage, float vectorX, float vectorZ, bool affectsArmour) {
    AsPlayer(victim, [&] { RecordPlayerDamage(damage, vectorX, vectorZ, attacker, affectsArmour); });
}

void Stage::RecordPlayerDamage(float damage, float vectorX, float vectorZ, int attacker, bool affectsArmour) {
    // bondviewCallRecordDamageKills and record_damage_kills: an open watch
    // redraws its gauges from the values before the hit.
    Cur().watch.RedrawGauges();
    // Two or more players take no damage once the round is over.
    if (!PadMovesPlayer()) {
        return;
    }
    // TODO(https://github.com/liz-baker/frigate/issues/389): the scenarios
    // change how much damage lands (Licence to Kill kills with any hit).
    if (!Cur().health.IsDead() && !cheatInvincible_) {
        RumblePlayer(0.25f);
    }
    const DamageEffects effects = Cur().health.RecordDamage(MakeHealthFrame(), damage, affectsArmour);

    if (effects.registered && playerCount_ >= 2 && attacker >= 0) {
        // A hit from behind counts for the attacker. The decomp reads
        // g_playerPlayerData[-1] for a hit by no player; nothing is counted.
        float angle = Cur().move.Theta() - (360.0f - ((GameAtan2f(vectorX, vectorZ) * 180.0f) / 3.1415927f));
        if (angle < 0.0f) {
            angle = -angle;
        }
        if (angle < 90.0f || angle > 270.0f) {
            stats_[static_cast<size_t>(attacker)].damageToBackside++;
        }
    }

    if (effects.killed) {
        if (playerCount_ >= 2) {
            CreditKill(attacker);
        }
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
    if (missionTimer_ - Cur().lifeStartTime < stats.shortestInning) {
        stats.shortestInning = missionTimer_ - Cur().lifeStartTime;
    }
    Cur().lifeStartTime = missionTimer_;
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
            if (playerCount_ >= 2) {
                music_.SetState(Game::MissionState::Dead);
            }
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
            } else {
                // You Only Live Twice: a player killed twice by the same
                // opponents (or himself) is out.
                int killedBy = 0;
                for (int player = 0; player < playerCount_; player++) {
                    killedBy += stats_[static_cast<size_t>(player)].killCounts[static_cast<size_t>(current_)];
                }
                if (multiplayer_->scenario != Game::MpScenario::YouOnlyLiveTwice || killedBy < 2) {
                    constexpr uint16_t kRespawnButtons = Buttons::A | Buttons::Z | Buttons::Start;
                    if ((Cur().previousButtons & kRespawnButtons) != 0) {
                        RespawnPlayer();
                    }
                }
            }
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
