#include "level/PlayerHealth.h"

#include <array>
#include <cstddef>

namespace Frigate::Level {

namespace {

// DamageType: how a hit is shown, by how many eighths of health the player
// has left.
struct DamageType {
    // field_0x8: the frame after which the display carries on only while the
    // flash does.
    int32_t displayEnd;
    int32_t flashStart;
    int32_t flashFull;
    int32_t flashEnd;
    float maxAlpha;
    int red;
    int green;
    int blue;
};

constexpr std::array<DamageType, 8> kDamageTypes = { {
    { 60, 0, 5, 40, 1.0f, 0xFF, 0xFF, 0xFF },
    { 60, 0, 5, 40, 1.0f, 0xFF, 0xFF, 0xFF },
    { 50, 0, 5, 30, 0.800000011921f, 0xFF, 0xFF, 0xFF },
    { 40, 0, 5, 25, 0.600000023842f, 0xFF, 0xFF, 0xFF },
    { 35, 0, 5, 22, 0.550000011921f, 0xFF, 0xFF, 0xFF },
    { 30, 0, 5, 19, 0.5f, 0xFF, 0xFF, 0xFF },
    { 30, 0, 5, 17, 0.449999988079f, 0xFF, 0xFF, 0xFF },
    { 30, 0, 5, 15, 0.40000000596f, 0xFF, 0xFF, 0xFF },
} };

// HealthDisplayDuration: when the gauges start to show, when they switch from
// the health before the hit to the health after it, and when they go.
struct HealthDisplayDuration {
    int32_t validStart;
    int32_t updateToReal;
    int32_t hide;
};

constexpr std::array<HealthDisplayDuration, 8> kHealthDisplayDurations = { {
    { 0, 40, 100 },
    { 0, 30, 80 },
    { 0, 20, 60 },
    { 0, 20, 60 },
    { 0, 20, 60 },
    { 0, 20, 50 },
    { 0, 20, 50 },
    { 0, 20, 50 },
} };

// WATCH_ANIMATION_0x5 and _0xC.
constexpr int32_t kWatchOpen = 5;
constexpr int32_t kWatchClosing = 12;

} // namespace

void PlayerHealth::Reset() {
    *this = PlayerHealth{};
    // The last hit's displays start at their mildest.
    healthDamageType_ = 7;
    damageType_ = 7;
}

void PlayerHealth::BeginLife() {
    const float actualHealth = actualHealth_;
    const float actualArmour = actualArmour_;
    Reset();
    actualHealth_ = actualHealth;
    actualArmour_ = actualArmour;
}

void PlayerHealth::Kill() {
    dead_ = 1;
}

DamageEffects PlayerHealth::RecordDamage(const HealthFrame& frame, float damage, bool affectsArmour) {
    DamageEffects effects;
    float dealt = frame.handicap * damage;

    // The gauges are redrawn from what they showed before the hit.
    if (frame.watchState != 0) {
        effects.redrawGauges = true;
    }

    if (frame.inTank) {
        dealt *= 0.25f;
    }

    if (dead_ == 0 && !frame.invincible && (damageShowTime_ < 0 || (frame.multiplayer && damageShowTime_ == 0))) {
        if (frame.watchState != kWatchOpen && frame.watchState != kWatchClosing) {
            effects.registered = true;
            oldHealth_ = health_;
            oldArmour_ = armour_;

            if (affectsArmour && dealt <= armour_ * actualArmour_) {
                armour_ = armour_ - (dealt / actualArmour_);
            } else {
                if (affectsArmour) {
                    dealt -= armour_ / actualArmour_;
                    armour_ = 0.0f;
                    actualArmour_ = 1.0f;
                }

                health_ = health_ - (dealt / actualHealth_);

                if (health_ <= 0.0f) {
                    effects.killed = true;
                }
            }

            if (damageShowTime_ < 0) {
                effects.push = true;
            }

            damageShowTime_ = 0;
            healthShowTime_ = 0;
        }
    }
    return effects;
}

DamageTickEffects PlayerHealth::Tick(const HealthFrame& frame) {
    DamageTickEffects effects;

    if (damageShowTime_ >= 0) {
        // The first frame of the hit.
        if (damageShowTime_ == 0) {
            effects.begin = true;

            damageType_ = static_cast<int32_t>(health_ * 8.0f);
            if (damageType_ >= 8) {
                damageType_ = 7;
            }
            // The US release reads before the table for a player killed by a
            // hit that takes more than a whole health bar.
            if (damageType_ < 0) {
                damageType_ = 0;
            }
        }

        const DamageType& type = kDamageTypes[static_cast<size_t>(damageType_)];
        if (type.displayEnd >= damageShowTime_ || type.flashEnd >= damageShowTime_) {
            if (dead_ == 0) {
                if (damageShowTime_ >= type.flashStart && damageShowTime_ <= type.flashEnd) {
                    const int32_t flashDone = damageShowTime_ - type.flashStart;
                    const int32_t flashFull = type.flashFull;
                    const int32_t total = type.flashEnd - type.flashStart;
                    float frac;

                    if (flashDone < flashFull) {
                        frac = (type.maxAlpha * static_cast<float>(flashDone)) / static_cast<float>(flashFull);
                    } else {
                        frac = (type.maxAlpha * static_cast<float>(total - flashDone)) /
                               static_cast<float>(total - flashFull);
                    }

                    effects.fade = true;
                    effects.red = type.red;
                    effects.green = type.green;
                    effects.blue = type.blue;
                    effects.fadeFrac = frac;
                }
            }

            if (frame.watchState == 0) {
                damageShowTime_ += frame.clockTimer;
            } else {
                damageShowTime_ += frame.speedFrames;
            }
        } else {
            // The display is over.
            damageShowTime_ = -1;
            effects.clearFade = true;

            if (dead_ == 0) {
                effects.end = true;
            }
        }
    }

    if (healthShowTime_ >= 0) {
        // The first frame of the hit.
        if (healthShowTime_ == 0) {
            healthDamageType_ = static_cast<int32_t>(health_ * 8.0f);
            if (healthDamageType_ >= 8) {
                healthDamageType_ = 7;
            }
            if (healthDamageType_ < 0) {
                healthDamageType_ = 0;
            }
        }

        if (dead_ == 0) {
            const HealthDisplayDuration& duration = kHealthDisplayDurations[static_cast<size_t>(healthDamageType_)];
            if (healthShowTime_ >= duration.validStart && duration.updateToReal >= healthShowTime_) {
                apparentHealth_ = oldHealth_;
                apparentArmour_ = oldArmour_;
                healthShowTime_ += frame.clockTimer;
            } else if (healthShowTime_ >= duration.validStart && duration.hide >= healthShowTime_) {
                apparentHealth_ = health_;
                apparentArmour_ = armour_;
                healthShowTime_ += frame.clockTimer;
            } else {
                healthShowTime_ = -1;
            }
        } else {
            healthShowTime_ = -1;
        }
    }
    return effects;
}

} // namespace Frigate::Level
