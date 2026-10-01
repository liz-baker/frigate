#include "level/PickupText.h"

namespace Frigate::Level {

namespace {

// PROPOBJ_STR_00 to PROPOBJ_STR_1F.
constexpr int32_t kSome = 0x01;
constexpr int32_t kA = 0x02;
constexpr int32_t kAn = 0x03;
constexpr int32_t kThe = 0x04;
constexpr int32_t kAmmo = 0x05;
constexpr int32_t kShotgunCartridge = 0x06;
constexpr int32_t kMagnumBullet = 0x07;
constexpr int32_t kGoldenBullet = 0x08;
constexpr int32_t kHandGrenade = 0x09;
constexpr int32_t kGrenadeRound = 0x0A;
constexpr int32_t kRocket = 0x0B;
constexpr int32_t kRemoteMine = 0x0C;
constexpr int32_t kProximityMine = 0x0D;
constexpr int32_t kTimedMine = 0x0E;
constexpr int32_t kThrowing = 0x0F;
constexpr int32_t kKnife = 0x10;
constexpr int32_t kKnives = 0x11;
constexpr int32_t kPlural = 0x12;
constexpr int32_t kDart = 0x13;
constexpr int32_t kExplosivePen = 0x14;
constexpr int32_t kExplosiveCase = 0x15;
constexpr int32_t kFlare = 0x16;
constexpr int32_t kPiton = 0x17;
constexpr int32_t kOfDynamite = 0x18;
constexpr int32_t kStick = 0x19;
constexpr int32_t kSticks = 0x1A;
constexpr int32_t kBug = 0x1B;
constexpr int32_t kMicroCamera = 0x1C;
constexpr int32_t kGoldenEyeKey = 0x1D;
constexpr int32_t kToken = 0x1E;
constexpr int32_t kPlastique = 0x1F;
// PROPOBJ_STR_38 to PROPOBJ_STR_3B: the keys, and a weapon the game has no
// text for.
constexpr int32_t kAKeyCard = 0x38;
constexpr int32_t kAYaleKey = 0x39;
constexpr int32_t kABoltKey = 0x3A;
constexpr int32_t kANewWeapon = 0x3B;

// The ITEM_IDS the weapon text names.
namespace Item {
constexpr int32_t Knife = 2;
constexpr int32_t ThrowKnife = 3;
constexpr int32_t Wppk = 4;
constexpr int32_t WppkSil = 5;
constexpr int32_t Tt33 = 6;
constexpr int32_t Skorpion = 7;
constexpr int32_t Ak47 = 8;
constexpr int32_t Uzi = 9;
constexpr int32_t Mp5k = 10;
constexpr int32_t Mp5kSil = 11;
constexpr int32_t Spectre = 12;
constexpr int32_t M16 = 13;
constexpr int32_t Fnp90 = 14;
constexpr int32_t Shotgun = 15;
constexpr int32_t AutoShot = 16;
constexpr int32_t SniperRifle = 17;
constexpr int32_t Ruger = 18;
constexpr int32_t GoldenGun = 19;
constexpr int32_t SilverWppk = 20;
constexpr int32_t GoldWppk = 21;
constexpr int32_t Laser = 22;
constexpr int32_t GrenadeLaunch = 24;
constexpr int32_t RocketLaunch = 25;
constexpr int32_t Grenade = 26;
constexpr int32_t TimedMine = 27;
constexpr int32_t ProximityMine = 28;
constexpr int32_t RemoteMine = 29;
constexpr int32_t BombCase = 33;
constexpr int32_t Plastique = 34;
constexpr int32_t FlarePistol = 35;
constexpr int32_t PitonGun = 36;
constexpr int32_t Bug = 47;
constexpr int32_t MicroCamera = 48;
constexpr int32_t GoldenEyeKey = 61;
constexpr int32_t KeyCard = 81;
constexpr int32_t KeyYale = 82;
constexpr int32_t KeyBolt = 83;
constexpr int32_t RocketRound = 86;
constexpr int32_t GrenadeRound = 87;
constexpr int32_t Token = 88;
} // namespace Item

int32_t Slot(int32_t slot) {
    return StringId(TextBank::PropObj, slot);
}

// append_text_ammo_amount_word.
void AppendAmountWord(std::string* out, const Lang& lang, int32_t ammoType, int32_t amount) {
    using namespace AmmoKind;
    switch (ammoType) {
        case Ammo9mm:
        case Ammo9mm2:
        case Rifle:
        case Plastique:
            out->append(lang.Get(Slot(kSome)));
            break;
        case Shotgun:
        case Grenade:
        case Rockets:
        case RemoteMine:
        case ProximityMine:
        case TimedMine:
        case Knife:
        case GrenadeRound:
        case Magnum:
        case GoldenGun:
        case Darts:
        case Flare:
        case Piton:
        case Dynamite:
        case Bug:
        case MicroCamera:
            out->append(lang.Get(Slot(amount == 1 ? kA : kSome)));
            break;
        case ExplosivePen:
        case BombCase:
            out->append(lang.Get(Slot(amount == 1 ? kAn : kSome)));
            break;
        case GoldenEyeKey:
        case Token:
            out->append(lang.Get(Slot(amount == 1 ? kThe : kSome)));
            break;
        default:
            break;
    }
}

// apped_text_ammotype.
void AppendAmmoName(std::string* out, const Lang& lang, int32_t ammoType, int32_t amount) {
    using namespace AmmoKind;
    if (ammoType == Ammo9mm || ammoType == Ammo9mm2 || ammoType == Rifle) {
        out->append(lang.Get(Slot(kAmmo)));
        return;
    }
    if (ammoType == Knife) {
        out->append(lang.Get(Slot(kThrowing)));
        out->append(lang.Get(Slot(amount == 1 ? kKnife : kKnives)));
        return;
    }
    if (ammoType == Dynamite) {
        out->append(lang.Get(Slot(amount == 1 ? kStick : kSticks)));
        out->append(lang.Get(Slot(kOfDynamite)));
        return;
    }

    int32_t name = -1;
    switch (ammoType) {
        case Shotgun:
            name = kShotgunCartridge;
            break;
        case Magnum:
            name = kMagnumBullet;
            break;
        case GoldenGun:
            name = kGoldenBullet;
            break;
        case Grenade:
            name = kHandGrenade;
            break;
        case GrenadeRound:
            name = kGrenadeRound;
            break;
        case Rockets:
            name = kRocket;
            break;
        case RemoteMine:
            name = kRemoteMine;
            break;
        case ProximityMine:
            name = kProximityMine;
            break;
        case TimedMine:
            name = kTimedMine;
            break;
        case Darts:
            name = kDart;
            break;
        case ExplosivePen:
            name = kExplosivePen;
            break;
        case BombCase:
            name = kExplosiveCase;
            break;
        case Flare:
            name = kFlare;
            break;
        case Piton:
            name = kPiton;
            break;
        case Bug:
            name = kBug;
            break;
        case MicroCamera:
            name = kMicroCamera;
            break;
        case GoldenEyeKey:
            name = kGoldenEyeKey;
            break;
        case Token:
            name = kToken;
            break;
        case Plastique:
            name = kPlastique;
            break;
        default:
            break;
    }
    if (name >= 0) {
        out->append(lang.Get(Slot(name)));
    }
    if (amount > 1) {
        out->append(lang.Get(Slot(kPlural)));
    }
}

int32_t WeaponSlot(int32_t item) {
    switch (item) {
        case Item::Knife:
            return 0x20;
        case Item::Wppk:
            return 0x21;
        case Item::WppkSil:
            return 0x22;
        case Item::Tt33:
            return 0x23;
        case Item::Skorpion:
            return 0x24;
        case Item::Ak47:
            return 0x25;
        case Item::Uzi:
            return 0x26;
        case Item::Mp5k:
            return 0x27;
        case Item::Mp5kSil:
            return 0x28;
        case Item::Spectre:
            return 0x29;
        case Item::M16:
            return 0x2A;
        case Item::Fnp90:
            return 0x2B;
        case Item::Shotgun:
            return 0x2C;
        case Item::AutoShot:
            return 0x2D;
        case Item::SniperRifle:
            return 0x2E;
        case Item::GrenadeLaunch:
            return 0x2F;
        case Item::RocketLaunch:
            return 0x30;
        case Item::Ruger:
            return 0x31;
        case Item::GoldenGun:
            return 0x32;
        case Item::Laser:
            return 0x33;
        case Item::FlarePistol:
            return 0x34;
        case Item::PitonGun:
            return 0x35;
        case Item::SilverWppk:
            return 0x36;
        case Item::GoldWppk:
            return 0x37;
        case Item::KeyCard:
            return kAKeyCard;
        case Item::KeyYale:
            return kAYaleKey;
        case Item::KeyBolt:
            return kABoltKey;
        default:
            return kANewWeapon;
    }
}

} // namespace

std::string AmmoCollectedText(const Lang& lang, int32_t ammoType, int32_t quantity) {
    std::string text;
    text.append(lang.Get(Slot(PropObjText::PickedUp)));
    AppendAmountWord(&text, lang, ammoType, quantity);
    AppendAmmoName(&text, lang, ammoType, quantity);
    text.append(".\n");
    return text;
}

std::string WeaponPickupText(const Lang& lang, const WeaponTable& table, int32_t item) {
    switch (item) {
        case Item::ThrowKnife:
        case Item::Grenade:
        case Item::TimedMine:
        case Item::ProximityMine:
        case Item::RemoteMine:
        case Item::BombCase:
        case Item::Plastique:
        case Item::Bug:
        case Item::MicroCamera:
        case Item::GoldenEyeKey:
        case Item::RocketRound:
        case Item::GrenadeRound:
        case Item::Token:
            return AmmoCollectedText(lang, table.AmmoType(item), 1);
        default:
            break;
    }
    std::string text(lang.Get(Slot(PropObjText::PickedUp)));
    text.append(lang.Get(Slot(WeaponSlot(item))));
    return text;
}

} // namespace Frigate::Level
