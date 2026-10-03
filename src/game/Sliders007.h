#pragma once

#include <cstdint>

#include "game/save/SaveData.h"

namespace Frigate::Game {

// slider_007_mode_reaction, _health, _damage and _accuracy (front.c): the 007
// options screen's sliders, at their cold-boot values. They are not saved.
struct Sliders007 {
    // 0 to 1: how much faster guards react.
    float reaction = 0.0f;
    // 0 to 10; the slider's position is the square root of a tenth of it.
    float health = 1.0f;
    float damage = 1.0f;
    float accuracy = 1.0f;
};

// get_007_reaction_speed, get_007_health_mod, get_007_damage_mod and
// get_007_accuracy_mod (file.c): the sliders apply on the 007 difficulty only.
inline float Get007ReactionSpeed(const Sliders007& sliders, int32_t difficulty) {
    return difficulty == Save::kDifficulty007 ? sliders.reaction : 0.0f;
}
inline float Get007HealthMod(const Sliders007& sliders, int32_t difficulty) {
    return difficulty == Save::kDifficulty007 ? sliders.health : 1.0f;
}
inline float Get007DamageMod(const Sliders007& sliders, int32_t difficulty) {
    return difficulty == Save::kDifficulty007 ? sliders.damage : 1.0f;
}
inline float Get007AccuracyMod(const Sliders007& sliders, int32_t difficulty) {
    return difficulty == Save::kDifficulty007 ? sliders.accuracy : 1.0f;
}

} // namespace Frigate::Game
