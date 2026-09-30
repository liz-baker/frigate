#pragma once

#include <cstdint>
#include <string>

#include "level/Lang.h"
#include "level/WeaponTable.h"

namespace Frigate::Level {

// PROPOBJ_STR_*: the slots of the pickup text bank (`LpropobjE`) that are not
// named by one of the items it describes.
namespace PropObjText {
constexpr int32_t PickedUp = 0x00;
constexpr int32_t PickedUpAKey = 0x3C;
constexpr int32_t PickedUpBodyArmour = 0x3D;
constexpr int32_t BodyArmour = 0x3E;
constexpr int32_t PickedUpSomething = 0x3F;
// What a door the player cannot open says.
constexpr int32_t DoorLocked = 0x40;
} // namespace PropObjText

// AMMOTYPE values the pickup text names.
namespace AmmoKind {
constexpr int32_t Ammo9mm = 1;
constexpr int32_t Ammo9mm2 = 2;
constexpr int32_t Rifle = 3;
constexpr int32_t Shotgun = 4;
constexpr int32_t Grenade = 5;
constexpr int32_t Rockets = 6;
constexpr int32_t RemoteMine = 7;
constexpr int32_t ProximityMine = 8;
constexpr int32_t TimedMine = 9;
constexpr int32_t Knife = 10;
constexpr int32_t GrenadeRound = 11;
constexpr int32_t Magnum = 12;
constexpr int32_t GoldenGun = 13;
constexpr int32_t Darts = 14;
constexpr int32_t ExplosivePen = 15;
constexpr int32_t BombCase = 16;
constexpr int32_t Flare = 17;
constexpr int32_t Piton = 18;
constexpr int32_t Dynamite = 19;
constexpr int32_t Bug = 20;
constexpr int32_t MicroCamera = 21;
constexpr int32_t GoldenEyeKey = 22;
constexpr int32_t Plastique = 23;
constexpr int32_t Token = 29;
} // namespace AmmoKind

// prepare_ammo_type_collection_text, for one player: "Picked up some ammo.\n".
std::string AmmoCollectedText(const Lang& lang, int32_t ammoType, int32_t quantity);

// generate_language_specific_text_for_weapon, for one player: what the
// message for picking up `item` says.
std::string WeaponPickupText(const Lang& lang, const WeaponTable& table, int32_t item);

} // namespace Frigate::Level
