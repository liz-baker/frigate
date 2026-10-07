#include "level/Stage.h"

#include "level/CuffType.h"
#include "level/StageAi.h"
#include "level/WeaponTable.h"

namespace Frigate::Level {

namespace {

constexpr float kDegToRad = 0.017453292f;

} // namespace

// getPropForHeldItem: the character model of a weapon in a character's hand
// (PROP_CHR*, or PROP_FLAG for a multiplayer scenario's flag), -1 for an item
// with none.
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
        case kItemToken:
            // PROP_FLAG.
            return 0x14D;
        default:
            return -1;
    }
}

void Stage::LoadBondBody() {
    if (Cur().bondChr >= 0) {
        // A body that exists already (the death camera's replays) is stood up
        // again, unless it has an animation going.
        Chr& chr = chrs_[Cur().bondChr];
        if (chr.model->Playback().anim != nullptr) {
            return;
        }
        chr.chrflags |= ChrFlag::Init;
        ai_->MergeKneelToStand(chr, 0.0f);
        PlaceBondBody();
        return;
    }

    // The weapon the body holds is the one in the right hand, or the one the
    // player starts with while the swirl shows the body.
    int32_t item = Cur().weapons.Ready() ? Cur().weapons.ItemInHandOrWatchMenu(HandRight) : kItemUnarmed;

    std::string_view body = "BODY_Formal_Wear";
    std::string_view head = "HEAD_Male_Brosnan_Default";
    if (PlayerCount() == 1) {
        // The body and head the Bond type and the folder's actor pick. Brosnan
        // is the only actor the game as built has.
        const int32_t cuff = Cur().intro.bondType.value_or(Cuff::kBlue);
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
    } else {
        head = multiplayer_->CharacterHead(current_);
        body = multiplayer_->CharacterBody(current_);
    }

    if (cameraMode_ == CameraMode::Swirl) {
        item = Cur().intro.startingWeaponRight;
    }

    // In a single-player game the body holds the weapon in the swirl: the
    // first-person hands show nothing, and stay that way until the body goes.
    if (PlayerCount() == 1 && Cur().weapons.Ready()) {
        Cur().weapons.RemoveItemInHand(HandLeft);
        Cur().weapons.RemoveItemInHand(HandRight);
    }

    const ChrTiming timing = { clock_.globalTimer, clock_.globalTimerDelta, clock_.clockTimer, clock_.frame };
    const int slot = chrs_.SpawnPlayerBody(Cur().move.PropId(), body, head, BondBodyYaw(), timing);
    if (slot < 0) {
        return;
    }
    Cur().bondChr = slot;
    chrs_.SetPlayerBodySlot(slot);
    Chr& chr = chrs_[slot];

    Cur().move.SetBody({ [this] { chrs_.DetectRooms(Cur().bondChr); },
                         [this](bool collides) { ChrAi::SetMoving(chrs_[Cur().bondChr], collides); } });

    if (const int model = HeldItemModel(item); model >= 0) {
        ChrHost host;
        host.prop = chr.prop;
        host.model = chr.model;
        host.weaponsHeld = &chr.weaponsHeld;
        host.hat = &chr.hat;
        objects_.GiveWeapon(host, chr.chrnum, model, item, 0);
    }
    ai_->MergeKneelToStand(chr, 0.0f);
}

void Stage::InstallPlayerBodyHooks() {
    PropRender::PlayerBodyHooks hooks;
    hooks.before = [this](int prop) { return PlayerBodyBefore(prop); };
    hooks.after = [this](int prop, PropRender::PlayerBodyMode mode) { PlayerBodyAfter(prop, mode); };
    render_.SetPlayerBody(std::move(hooks));
}

PropRender::PlayerBodyMode Stage::PlayerBodyBefore(int prop) {
    using Mode = PropRender::PlayerBodyMode;
    const int index = PlayerOfProp(prop);
    if (index < 0) {
        return Mode::Tick;
    }
    PlayerState& state = players_[static_cast<size_t>(index)];
    Chr& chr = chrs_[objects_.Props()[prop].object];

    // The body moves on, and the count of bodies posed advances, in the pass of
    // the player first in the shuffle.
    const bool firstVisit = PositionInShuffled(current_) == 0;
    if (firstVisit) {
        chr.hidden &= ~ChrHidden::Freeze;
    }

    // The cinematic camera (cameramode 1) ticks the body as a character.
    if (state.frozenCamera) {
        state.move.SetFootprintCounts(false);
        return Mode::Tick;
    }
    // A player's own body is not shown to them, nor is any in a single-player
    // game.
    if (playerCount_ < 2 || index == current_) {
        return Mode::Hidden;
    }

    // Nothing resets g_PlayerTickCount: the first two bodies the process poses
    // are left as they are.
    static int32_t playerTickCount = 0;
    if (firstVisit) {
        playerTickCount++;
    }

    ChrActions::PlayerBody body;
    body.dead = state.health.IsDead();
    body.crouch = state.move.CrouchPosition();
    body.speedSideways = state.move.SpeedSideways();
    body.speedForwards = state.move.SpeedForwards();
    body.speedTheta = state.move.SpeedTheta();
    body.headAnimation = state.move.HeadAnimation();
    body.theta = state.move.Theta();
    body.aimPitch = state.sightPitch;
    body.aimYaw = state.sightYaw;
    if (state.weapons.Ready()) {
        body.firing = { state.weapons.Hands(HandRight).flashVisible, state.weapons.Hands(HandLeft).flashVisible };
    }
    body.deathClips = state.move.DeathClips();
    body.position = state.move.CollisionPosition();
    body.tile = state.move.Tile();
    body.clip = state.bodyClip;
    body.clipSpeed = state.bodyClipSpeed;
    body.lean = state.bodyLean;
    aiActions_->PosePlayerBody(chr, body, playerTickCount >= 2);
    state.bodyClip = body.clip;
    state.bodyClipSpeed = body.clipSpeed;
    state.bodyLean = body.lean;
    return Mode::Remote;
}

void Stage::PlayerBodyAfter(int prop, PropRender::PlayerBodyMode mode) {
    using Mode = PropRender::PlayerBodyMode;
    const int index = PlayerOfProp(prop);
    if (index < 0) {
        return;
    }
    PlayerState& state = players_[static_cast<size_t>(index)];
    Prop& record = objects_.Props()[prop];
    if (mode == Mode::Tick) {
        state.move.SetFootprintCounts(true);
        // The body's head position the game also copies to the camera's
        // position is rebuilt from the collision position at the start of
        // every move, and the cinematic camera does not read it.
        state.move.AdoptBodyPosition(record.position, record.stan, level_.StanFile());
        return;
    }

    // Where the weapons of the body were last seen, which a shot's beam is
    // matched against (gunCreateBeamForHand); a muzzle not seen for a while
    // falls back to the first-person gun's.
    Chr& chr = chrs_[record.object];
    for (int hand = 0; hand < 2; hand++) {
        const auto slot = static_cast<size_t>(hand);
        if (aiActions_->GunMuzzle(chr, hand, Cur().camera.ViewToWorld(), &state.characterMuzzle[slot])) {
            state.characterMuzzleTick[slot] = clock_.frame;
        } else if (state.characterMuzzleTick[slot] < clock_.frame - 1 && state.weapons.Ready()) {
            state.characterMuzzle[slot] = state.weapons.Hands(static_cast<Hand>(hand)).muzzlePosition;
        }
    }

    chr.hidden |= ChrHidden::Freeze;
    record.position = state.move.CollisionPosition();
    record.stan = state.move.Tile();
}

void Stage::GiveBodyWeapon(int hand) {
    // sub_GAME_7F09B398: a body with nothing in the hand takes the weapon the
    // hand now holds.
    if (Cur().bondChr < 0 || !Cur().weapons.Ready()) {
        return;
    }
    Chr& chr = chrs_[Cur().bondChr];
    if (chr.weaponsHeld[static_cast<size_t>(hand)] >= 0) {
        return;
    }
    const int32_t item = Cur().weapons.WeaponId(static_cast<Hand>(hand));
    const int model = HeldItemModel(item);
    if (model < 0) {
        return;
    }
    ChrHost host;
    host.prop = chr.prop;
    host.model = chr.model;
    host.weaponsHeld = &chr.weaponsHeld;
    host.hat = &chr.hat;
    // PROPFLAG_WEAPON_LEFTHANDED.
    objects_.GiveWeapon(host, chr.chrnum, model, item, hand == HandLeft ? 0x10000000 : 0);
}

void Stage::ReleaseBodyWeapon(int hand) {
    // sub_GAME_7F09B368, chrSetWeaponFlag4 on the body.
    if (Cur().bondChr < 0) {
        return;
    }
    aiBridge_->RemoveHeldItem(chrs_[Cur().bondChr], hand);
}

float Stage::BondBodyYaw() const {
    // bondviewGetPlayerYawRadians.
    return (360.0f - Cur().move.Theta()) * kDegToRad;
}

void Stage::PlaceBondBody() {
    // setsuboffset from the prop's position, and setsubroty.
    const Vec3& position = objects_.Props()[Cur().move.PropId()].position;
    ModelPlayback& playback = chrs_[Cur().bondChr].model->Playback();
    playback.SetPosition({ position[0], position[1], position[2] });
    playback.SetHeading(BondBodyYaw());
}

void Stage::StartBondDeathAnimation() {
    // The body plays the death animation the player's own model is playing,
    // at half speed.
    Chr& chr = chrs_[Cur().bondChr];
    chr.model->Playback().SetAnimation(Cur().move.PlayingClip(), Cur().move.PlayingMirrored(), 0.0f, 0.5f, 0.0f);
    chr.actionType = ChrAction::BondDie;
    chr.sleep = 0;
    chr.chrflags |= ChrFlag::Init;
    PlaceBondBody();
}

void Stage::RemoveBondBody() {
    // Only a single-player game's body goes; a multiplayer character stays for
    // the others to see.
    if (Cur().bondChr < 0 || PlayerCount() != 1) {
        return;
    }
    render_.CleanupChr(Cur().move.PropId());
    Cur().bondChr = -1;
    chrs_.SetPlayerBodySlot(-1);
    Cur().move.SetBody({});
    forceDisarm_ = 1;
    Cur().move.UpdateRoom(level_.StanFile());
}

} // namespace Frigate::Level
