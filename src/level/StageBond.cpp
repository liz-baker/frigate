#include "level/Stage.h"

#include "level/CuffType.h"
#include "level/WeaponTable.h"

namespace Frigate::Level {

namespace {

constexpr float kDegToRad = 0.017453292f;

// getPropForHeldItem: the character model of a weapon in a character's hand
// (PROP_CHR*), -1 for an item with none. ITEM_TOKEN, a multiplayer scenario's
// flag, is not an item a single-player stage holds.
int HeldItemModel(int32_t item) {
    switch (item) {
        case kItemKnife:
            return 0xBA;
        case kItemPp7:
        case kItemSilverPp7:
        case kItemGoldPp7:
            return 0xBF;
        case kItemKlobb:
            return 0xC1;
        case kItemKf7:
            return 0xB8;
        case kItemZmg:
            return 0xC3;
        case kItemD5k:
            return 0xBD;
        case kItemPhantom:
            return 0xC2;
        case kItemAr33:
            return 0xBC;
        case kItemRcp90:
            return 0xC5;
        case kItemShotgun:
            return 0xC0;
        case kItemGrenadeLauncher:
            return 0xB9;
        case kItemGrenade:
            return 0xC4;
        case kItemRemoteMine:
            return 0xC7;
        case kItemProximityMine:
            return 0xC8;
        case kItemTimedMine:
            return 0xC9;
        case kItemRuger:
            return 0xBE;
        case kItemLaser:
            return 0xBB;
        case kItemSniperRifle:
            return 0xD2;
        case kItemD5kSilenced:
            return 0xCE;
        case kItemDd44:
            return 0xCD;
        case kItemPp7Silenced:
            return 0xCC;
        case kItemThrowKnife:
            return 0xD1;
        case kItemAutoShotgun:
            return 0xCF;
        case kItemRocketLauncher:
            return 0xD3;
        case kItemGoldenGun:
            return 0xD0;
        default:
            return -1;
    }
}

} // namespace

void Stage::LoadBondBody() {
    if (bondChr_ >= 0) {
        // A body that exists already (the death camera's replays) is stood up
        // again, unless it has an animation going.
        Chr& chr = chrs_[bondChr_];
        if (chr.model->Playback().anim != nullptr) {
            return;
        }
        chr.chrflags |= ChrFlag::Init;
        ai_->MergeKneelToStand(chr, 0.0f);
        PlaceBondBody();
        return;
    }

    // The body and head the Bond type and the folder's actor pick. Brosnan is
    // the only actor the game as built has.
    const int32_t cuff = intro_.bondType.value_or(Cuff::kBlue);
    std::string_view body = "BODY_Formal_Wear";
    std::string_view head = "HEAD_Male_Brosnan_Default";
    switch (cuff) {
        case Cuff::kBoiler:
            body = "BODY_Special_Operations_Uniform";
            break;
        case Cuff::kJungle:
            body = "BODY_Jungle_Fatigues";
            break;
        case Cuff::kSnow:
            body = "BODY_Parka";
            break;
        case Cuff::kBrosnan:
        case Cuff::kConnery:
        case Cuff::kDalton:
        case Cuff::kMoore:
            body = "BODY_Brosnan_Tuxedo";
            break;
        case Cuff::kFolder:
            body = "BODY_Brosnan_Tuxedo";
            break;
        default:
            break;
    }
    switch (cuff) {
        case Cuff::kBoiler:
            head = "HEAD_Male_Brosnan_Boiler";
            break;
        case Cuff::kJungle:
            head = "HEAD_Male_Brosnan_Jungle";
            break;
        case Cuff::kBrosnan:
        case Cuff::kConnery:
        case Cuff::kDalton:
        case Cuff::kMoore:
        case Cuff::kFolder:
            head = "HEAD_Male_Brosnan_Tuxedo";
            break;
        default:
            break;
    }

    // The body holds the weapon in the swirl: the first-person hands show
    // nothing, and stay that way until the body goes.
    if (weapons_.Ready()) {
        weapons_.RemoveItemInHand(HandLeft);
        weapons_.RemoveItemInHand(HandRight);
    }

    const ChrTiming timing = { clock_.globalTimer, clock_.globalTimerDelta, clock_.clockTimer, clock_.frame };
    const int slot = chrs_.SpawnPlayerBody(player_.PropId(), body, head, BondBodyYaw(), timing);
    if (slot < 0) {
        return;
    }
    bondChr_ = slot;
    Chr& chr = chrs_[slot];

    player_.SetBody({ [this] { chrs_.DetectRooms(bondChr_); },
                      [this](bool collides) { ChrAi::SetMoving(chrs_[bondChr_], collides); } });

    // In the swirl the right hand holds the weapon the player starts with.
    if (const int model = HeldItemModel(intro_.startingWeaponRight); model >= 0) {
        ChrHost host;
        host.prop = chr.prop;
        host.model = chr.model;
        host.weaponsHeld = &chr.weaponsHeld;
        host.hat = &chr.hat;
        objects_.GiveWeapon(host, chr.chrnum, model, intro_.startingWeaponRight, 0);
    }
    ai_->MergeKneelToStand(chr, 0.0f);
}

float Stage::BondBodyYaw() const {
    // bondviewGetPlayerYawRadians.
    return (360.0f - player_.Theta()) * kDegToRad;
}

void Stage::PlaceBondBody() {
    // setsuboffset from the prop's position, and setsubroty.
    const Vec3& position = objects_.Props()[player_.PropId()].position;
    ModelPlayback& playback = chrs_[bondChr_].model->Playback();
    playback.SetPosition({ position[0], position[1], position[2] });
    playback.SetHeading(BondBodyYaw());
}

void Stage::StartBondDeathAnimation() {
    // The body plays the death animation the player's own model is playing,
    // at half speed.
    Chr& chr = chrs_[bondChr_];
    chr.model->Playback().SetAnimation(player_.PlayingClip(), player_.PlayingMirrored(), 0.0f, 0.5f, 0.0f);
    chr.actionType = ChrAction::BondDie;
    chr.sleep = 0;
    chr.chrflags |= ChrFlag::Init;
    PlaceBondBody();
}

void Stage::RemoveBondBody() {
    if (bondChr_ < 0) {
        return;
    }
    render_.CleanupChr(player_.PropId());
    bondChr_ = -1;
    player_.SetBody({});
    forceDisarm_ = 1;
    player_.UpdateRoom(level_.StanFile());
}

} // namespace Frigate::Level
