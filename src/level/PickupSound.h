#pragma once

#include <cstdint>

namespace Frigate::Level {

// set_sound_effect_for_ammo_collection and set_sound_effect_for_weapontype_collection:
// the sound a pickup makes, by ammo type (AmmoKind) and by item (kItem*).
void PlayAmmoCollectionSound(int32_t ammoType);
void PlayWeaponCollectionSound(int32_t item);

} // namespace Frigate::Level
