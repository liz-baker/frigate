#pragma once

#include <cstdint>

namespace Frigate::Level {

// What a tick of the damage display asks the rest of the HUD to do.
struct DamageTickEffects {
    // The first frame of a hit: the ammo counter, crosshair, messages, upper
    // text and countdown are hidden.
    bool begin = false;
    // The hit's display is over: they come back (unless the player is dead).
    bool end = false;
    // The view is washed with this colour (currentPlayerSetFadeColour).
    bool fade = false;
    int red = 0;
    int green = 0;
    int blue = 0;
    float fadeFrac = 0.0f;
    // The wash is cleared to white at nothing.
    bool clearFade = false;
};

// What a hit asks of the rest of the stage.
struct DamageEffects {
    // The hit got past the checks that turn it away (a dead or invincible
    // player, one still showing a hit, the open watch): it counts.
    bool registered = false;
    // The player's gauges, which a hit while the watch is up redraws from the
    // values they had.
    bool redrawGauges = false;
    // The hit pushes the player along (bondshotspeed += 2 * vector).
    bool push = false;
    // The hit killed the player (bondviewKillCurrentPlayer).
    bool killed = false;
};

// What the damage code reads each time it runs.
struct HealthFrame {
    // watch_animation_state.
    int32_t watchState = 0;
    int32_t clockTimer = 1;
    float globalTimerDelta = 1.0f;
    // speedgraphframes.
    int32_t speedFrames = 1;
    // g_PlayerIsInTank == 1 and g_PlayerInvincible.
    bool inTank = false;
    bool invincible = false;
    // g_playerPerm->handicap.
    float handicap = 1.0f;
    // getPlayerCount() >= 2: a hit may land on the first tick of the previous
    // one's display.
    bool multiplayer = false;
};

// The player's health, armour and the state of their display and death
// (bondhealth, bondarmour, damageshowtime, bonddead and their neighbours in the
// player record).
class PlayerHealth {
  public:
    // The values a life starts with (init_player_BONDdata and the reset at the
    // top of bondviewLoadSetupIntroSection).
    void Reset();
    // init_player_BONDdata for a player who has played a life already: the
    // health, armour and display state of a new one. The cheats' bar sizes
    // (actual_health and actual_armor) are not part of it.
    void BeginLife();

    // currentPlayerGetHealth and currentPlayerGetArmor.
    float Health() const {
        return health_;
    }
    float Armour() const {
        return armour_;
    }
    // bondviewAddCurrentPlayerArmor: the armour is replaced.
    void SetArmour(float armour) {
        armour_ = armour;
    }
    void SetHealth(float health) {
        health_ = health;
    }
    // The 2x and 10x health and armour cheats: the full bar, and what damage
    // divides by (actual_health and actual_armor).
    void SetActualHealth(float actual) {
        actualHealth_ = actual;
    }
    void SetActualArmour(float actual) {
        actualArmour_ = actual;
    }
    float ActualHealth() const {
        return actualHealth_;
    }
    float ActualArmour() const {
        return actualArmour_;
    }
    // bonddead: 0 alive, 1 just killed, 2 dead.
    int Dead() const {
        return dead_;
    }
    bool IsDead() const {
        return dead_ != 0;
    }
    // bondviewGetIfCurrentPlayerDamageShowTime and ...HealthShowTime.
    bool DamageShowing() const {
        return damageShowTime_ >= 0;
    }
    bool HealthShowing() const {
        return healthShowTime_ > 0;
    }
    int32_t HealthShowTime() const {
        return healthShowTime_;
    }
    // apparenthealth and apparentarmour: what the gauges show.
    float ApparentHealth() const {
        return apparentHealth_;
    }
    float ApparentArmour() const {
        return apparentArmour_;
    }
    // apparenthealth = bondhealth and apparentarmour = bondarmour.
    void SyncApparent() {
        apparentHealth_ = health_;
        apparentArmour_ = armour_;
    }
    bool RedBloodFinished() const {
        return redBloodFinished_;
    }
    bool DeathAnimFinished() const {
        return deathAnimFinished_;
    }
    void SetRedBloodFinished() {
        redBloodFinished_ = true;
    }
    void SetDeathAnimFinished() {
        deathAnimFinished_ = true;
    }
    // bonddead becomes 2 once the blood has started.
    void SetDead(int dead) {
        dead_ = dead;
    }

    // record_damage_kills' share of the player's own health: the player takes
    // `damage`; `affectsArmour` is false for gas.
    DamageEffects RecordDamage(const HealthFrame& frame, float damage, bool affectsArmour);
    // bondviewKillCurrentPlayer's change to the player's health state; the rest
    // of the kill (the watch, the guns, the camera) belongs to the stage.
    void Kill();

    // bondviewPlayerTickDamageAndHealth.
    DamageTickEffects Tick(const HealthFrame& frame);

  private:
    float health_ = 1.0f;
    float armour_ = 0.0f;
    float oldHealth_ = 1.0f;
    float oldArmour_ = 0.0f;
    float apparentHealth_ = 1.0f;
    float apparentArmour_ = 0.0f;
    // actual_health and actual_armor: what damage divides by.
    float actualHealth_ = 1.0f;
    float actualArmour_ = 1.0f;
    int32_t damageShowTime_ = -1;
    int32_t healthShowTime_ = -1;
    int32_t damageType_ = 0;
    int32_t healthDamageType_ = 0;
    int dead_ = 0;
    bool redBloodFinished_ = false;
    bool deathAnimFinished_ = false;
};

} // namespace Frigate::Level
