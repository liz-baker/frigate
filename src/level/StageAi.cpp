#include "level/StageAi.h"

#include <cmath>

#include "level/Frustum.h"

namespace Frigate::Level {

namespace {

constexpr float kTicksPerSecond = 60.0f;
// How far past the screen edge chrIsPosOffScreen still counts a position as on
// screen.
constexpr float kOffScreenMargin = 200.0f;
// The CHRBYTE of PLAYERFLAG_* the control commands test.
constexpr uint8_t kPlayerFlagLockControls = 1;
constexpr uint8_t kPlayerFlagNoControl = 2;
constexpr uint8_t kPlayerFlagNoTimer = 4;
// countdownTimerSetVisible's bit for the AI's timer and the one the control
// commands use.
constexpr int kCountdownAiBit = 1;
constexpr int kCountdownControlBit = 16;

} // namespace

AiPlayer Stage::AiBridge::Player() const {
    AiPlayer player;
    player.prop = stage_.player_.PropId();
    // bondviewGetPlayerYawRadians: DegToRad(360.0f - vv_theta).
    player.yaw = ((360.0f - stage_.player_.Theta()) * 6.2831855f) / 360.0f;
    player.health = stage_.health_.Health();
    player.dead = stage_.health_.IsDead();
    // bondviewGetVisibleToGuardsFlag is cleared only by the invisibility cheat.
    player.inTank = stage_.player_.InTank();
    return player;
}

void Stage::AiBridge::SetPlayerCollides(bool collides) {
    stage_.player_.SetCollides(collides);
}

int Stage::AiBridge::Difficulty() const {
    return stage_.difficulty_;
}

int Stage::AiBridge::StageNumber() const {
    return static_cast<int>(stage_.currentStage_);
}

float Stage::AiBridge::StageSeconds() const {
    return static_cast<float>(stage_.mpTicks_) / kTicksPerSecond;
}

float Stage::AiBridge::PowerOnSeconds() const {
    return static_cast<float>(stage_.powerTicks_) / kTicksPerSecond;
}

int Stage::AiBridge::PlayerCount() const {
    return kPlayerCount;
}

float Stage::AiBridge::FarFogIntensitySquared() const {
    return stage_.fog_.ScaledFarFogIntensitySquared();
}

bool Stage::AiBridge::RoomRendered(int room) const {
    return stage_.visibility_.RoomRendered(room);
}

bool Stage::AiBridge::RoomsSharePortal(int roomA, int roomB) const {
    for (const BgPortal& portal : stage_.level_.Portals()) {
        if ((portal.room1 == roomA && portal.room2 == roomB) || (portal.room2 == roomA && portal.room1 == roomB)) {
            return true;
        }
    }
    return false;
}

bool Stage::AiBridge::PositionOffScreen(const Vec3& position, int tile) const {
    // chrIsPosOffScreen.
    bool offScreen = true;
    if (tile == kNoTile) {
        return offScreen;
    }
    const int room = stage_.level_.StanFile().TileRoom(tile);
    if (stage_.visibility_.RoomRendered(room) &&
        stage_.fog_.PositionIsVisibleThroughFog(position, 0.0f, stage_.camera_.WorldPosition(),
                                                stage_.camera_.WorldToScreen())) {
        ScreenRect bounds;
        if (stage_.visibility_.GetRoomBounds2d(room, bounds)) {
            offScreen = !PositionOnScreenBox(stage_.camera_.Box(), position, kOffScreenMargin,
                                             { bounds.left, bounds.top, bounds.right, bounds.bottom });
        } else {
            offScreen = !PositionOnScreen(stage_.camera_.Frustum(), position, kOffScreenMargin);
        }
    }
    return offScreen;
}

bool Stage::AiBridge::PlayerAimsAtSphere(const Vec3& centre, float radius) const {
    // sub_GAME_7F068190: the ray from the eye through the crosshair, in view
    // space (transformAndNormalizeByLength2Dto3D with length 1).
    const PlayerCamera& camera = stage_.camera_;
    const GunAimState& aim = stage_.weapons_.Aim();
    const float y = ((camera.ScreenHeight() * 0.5f) - (aim.crosshairScreenY - camera.ScreenTop())) * camera.ScaleY();
    const float x = ((aim.crosshairScreenX - camera.ScreenLeft()) - (camera.ScreenWidth() * 0.5f)) * camera.ScaleX();
    const float z = -1.0f;
    const float norm = 1.0f / std::sqrt((x * x) + (y * y) + (z * z));
    const Vec3 direction = { x * norm, y * norm, -1.0f * norm };

    // mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &centre).
    const Gu::MatrixF& m = camera.WorldToScreen();
    const Vec3 vector = { (m[0][0] * centre[0]) + (m[1][0] * centre[1]) + (m[2][0] * centre[2]) + m[3][0],
                          (m[0][1] * centre[0]) + (m[1][1] * centre[1]) + (m[2][1] * centre[2]) + m[3][1],
                          (m[0][2] * centre[0]) + (m[1][2] * centre[1]) + (m[2][2] * centre[2]) + m[3][2] };

    // projectileTestPropBoundingSphere.
    const float along = (direction[0] * vector[0]) + (direction[1] * vector[1]) + (direction[2] * vector[2]);
    if (along > 0.0f) {
        const float a = (direction[0] * direction[0]) + (direction[1] * direction[1]) + (direction[2] * direction[2]);
        const float b = (vector[0] * vector[0]) + (vector[1] * vector[1]) + (vector[2] * vector[2]);
        return (b - (radius * radius)) * a <= along * along;
    }
    return false;
}

void Stage::AiBridge::AlarmActivate() {
    stage_.ActivateAlarm();
}

void Stage::AiBridge::AlarmDeactivate() {
    stage_.DeactivateAlarm();
}

bool Stage::AiBridge::AlarmActive() const {
    return stage_.alarmTimer_ > 0;
}

bool Stage::AiBridge::GasLeaking() const {
    return stage_.gasTimer_ > 0.0f;
}

void Stage::AiBridge::GasLeak() {
    // The command's emitPos is the origin.
    stage_.TriggerGas({ 0.0f, 0.0f, 0.0f });
}

int Stage::AiBridge::PlayerWeapon(int hand) const {
    return stage_.weapons_.Ready() ? stage_.weapons_.WeaponId(static_cast<Hand>(hand)) : kItemUnarmed;
}

int Stage::AiBridge::PlayerAmmo(int ammoType) const {
    // currentPlayerGetAmmoCount: what is held and what is loaded in a hand's
    // gun of that ammo type.
    const PlayerWeapons& weapons = stage_.weapons_;
    if (!weapons.Ready() || ammoType < 0 || ammoType >= 30) {
        return 0;
    }
    int total = weapons.AmmoHeld(ammoType);
    for (const Hand hand : { HandRight, HandLeft }) {
        if (weapons.Table().AmmoType(weapons.WeaponId(hand)) == ammoType) {
            total += weapons.Hands(hand).ammoInMagazine;
        }
    }
    return total;
}

void Stage::AiBridge::PlayerEquip(int hand, int item) {
    if (stage_.weapons_.Ready()) {
        stage_.weapons_.EquipWeapon(static_cast<Hand>(hand), item);
    }
}

void Stage::AiBridge::PlayerUnequip(int hand, int item) {
    if (stage_.weapons_.Ready()) {
        stage_.weapons_.UnEquipWeapon(static_cast<Hand>(hand), item);
    }
}

void Stage::AiBridge::PlayerRemoveItemInHand(int hand) {
    if (stage_.weapons_.Ready()) {
        stage_.weapons_.RemoveItemInHand(static_cast<Hand>(hand));
    }
}

bool Stage::AiBridge::PlayerHasProp(int prop) const {
    for (const InvItem& item : stage_.intro_.inventory.Items()) {
        if (item.IsProp() && item.prop == prop) {
            return true;
        }
    }
    return false;
}

bool Stage::AiBridge::WeaponHasFlag(int item, uint32_t flag) const {
    return stage_.weapons_.Ready() && stage_.weapons_.Table().HasFlag(item, flag);
}

void Stage::AiBridge::ShowBottomText(int text) {
    stage_.hud_.BottomShow(stage_.lang_.Get(text));
}

void Stage::AiBridge::ShowTopText(int text) {
    stage_.hud_.TopShow(stage_.lang_.Get(text));
}

void Stage::AiBridge::BondControl(bool enable, uint8_t flags) {
    Hud& hud = stage_.hud_;
    if (enable) {
        hud.SetSightVisible(SightReason::NoControl, true);
        hud.SetAmmoVisible(AmmoReason::NoControl, true);
        hud.MessagesOn(HudFlag::NoControl);
        hud.ClearUpperTextFlag(2);
        stage_.countdownHidden_ &= ~kCountdownControlBit;
        stage_.timerActive_ = true;
        return;
    }
    hud.SetSightVisible(SightReason::NoControl, false);
    hud.SetAmmoVisible(AmmoReason::NoControl, false);
    if ((flags & kPlayerFlagNoControl) == 0) {
        hud.MessagesOff(HudFlag::NoControl);
    }
    if ((flags & kPlayerFlagLockControls) == 0) {
        hud.SetUpperTextFlag(HudFlag::NoControl);
    }
    if ((flags & kPlayerFlagNoTimer) == 0) {
        stage_.countdownHidden_ |= kCountdownControlBit;
    }
    // TODO(https://github.com/liz-baker/frigate/issues/140): is_timer_active
    // freezes the player's movement (bondviewFrozenMoveBond) while the camera
    // is in first person.
    stage_.timerActive_ = false;
}

void Stage::AiBridge::CountdownShow(bool show) {
    // countdownTimerSetVisible(1, TRUE) shows it; FALSE hides it.
    if (show) {
        stage_.countdownHidden_ &= ~kCountdownAiBit;
    } else {
        stage_.countdownHidden_ |= kCountdownAiBit;
    }
}

void Stage::AiBridge::CountdownSet(float ticks) {
    stage_.countdownTime_ = ticks;
}

void Stage::AiBridge::CountdownRunning(bool running) {
    stage_.countdownRunning_ = running;
}

bool Stage::AiBridge::CountdownIsRunning() const {
    return stage_.countdownRunning_;
}

float Stage::AiBridge::CountdownValue() const {
    return stage_.countdownTime_;
}

int Stage::AiBridge::CameraMode() const {
    return static_cast<int>(stage_.cameraMode_);
}

void Stage::AiBridge::CameraReturnToBond() {
    stage_.cameraMode_ = Frigate::Level::CameraMode::FirstPersonNoInput;
}

void Stage::AiBridge::CameraLookAtBondFromPad(int pad) {
    // TODO(https://github.com/liz-baker/frigate/issues/140): the cutscene camera
    // that looks at the player from `pad`.
    (void)pad;
    stage_.cameraMode_ = Frigate::Level::CameraMode::PosEnd;
}

void Stage::AiBridge::CameraSwitch(const PropDef& cameraPosition, int lookAtBond, int unused) {
    // TODO(https://github.com/liz-baker/frigate/issues/140): the cutscene camera
    // placed by the camera-position record.
    (void)cameraPosition;
    (void)lookAtBond;
    (void)unused;
    stage_.cameraMode_ = Frigate::Level::CameraMode::PosEnd;
}

void Stage::AiBridge::CameraOrbitPad(int pad, int latDistance, int vertDistance, int orbitSpeed60, int yOffset,
                                     int initialRotation) {
    // TODO(https://github.com/liz-baker/frigate/issues/140): the cutscene camera
    // that orbits the pad.
    (void)pad;
    (void)latDistance;
    (void)vertDistance;
    (void)orbitSpeed60;
    (void)yOffset;
    (void)initialRotation;
    stage_.cameraMode_ = Frigate::Level::CameraMode::PosEnd;
}

void Stage::AiBridge::ScreenFade(bool toBlack) {
    if (stage_.stopTime_ == 2) {
        return;
    }
    // CHRAI_TICKRATE_F.
    constexpr float kFadeTicks = 60.0f;
    stage_.colour_.SetColour(0, 0, 0, toBlack ? 0.0f : 1.0f);
    stage_.colour_.SetFrac(kFadeTicks, toBlack ? 1.0f : 0.0f);
}

bool Stage::AiBridge::ScreenFadeCompleted() const {
    return stage_.colour_.IsComplete();
}

void Stage::AiBridge::BondForceMove(float x, float z) {
    // TODO(https://github.com/liz-baker/frigate/issues/140): g_ForceBondMoveOffset,
    // the scripted movement of the player in a cutscene.
    (void)x;
    (void)z;
}

void Stage::AiBridge::SwitchSky() {
    // fogSwitchToSolosky2(1.0).
    stage_.fog_.SwitchToSolosky2(1.0f);
    stage_.camera_.SetZRange(stage_.fog_.ZNear(), stage_.fog_.ZFar());
}

int Stage::AiBridge::FolderActor() const {
    // fileGetBondForCurrentFolder: the actor of the save folder the game was
    // started from. The attract demos run with no folder, which reads as the
    // first actor.
    return 0;
}

void Stage::AiBridge::EndLevel() {
    // aiEndLevel: bossReturnTitleStage.
    stage_.levelEndRequested_ = true;
}

int Stage::AiBridge::StopTime() const {
    return stage_.stopTime_;
}

void Stage::AiBridge::SetStopTime(int state) {
    stage_.stopTime_ = state;
}

int Stage::AiBridge::Credits() const {
    return stage_.credits_;
}

void Stage::AiBridge::SetCredits(int state) {
    // TODO(https://github.com/liz-baker/frigate/issues/140): the credits roll,
    // which would take the state to 2 when it is done.
    stage_.credits_ = state;
}

bool Stage::AiBridge::PlayerInvincible() const {
    return stage_.playerInvincible_;
}

void Stage::AiBridge::SetPlayerInvincible(bool invincible) {
    stage_.playerInvincible_ = invincible;
}

void Stage::AiBridge::SetBondKilledInAction() {
    stage_.bondKilledInAction_ = true;
}

// The sound commands.
void Stage::AiBridge::SfxPlay(int channel, int sound) {
    stage_.aiSounds_->Play(channel, static_cast<int16_t>(sound));
}

void Stage::AiBridge::SfxStop(int channel) {
    stage_.aiSounds_->Stop(channel);
}

void Stage::AiBridge::SfxSetVolume(int channel, int volume, int fadeTicks) {
    stage_.aiSounds_->SetVolume(channel, volume, fadeTicks);
}

void Stage::AiBridge::SfxFadeVolume(int channel, float volume, int fadeTicks) {
    stage_.aiSounds_->FadeVolume(channel, volume, fadeTicks);
}

void Stage::AiBridge::SfxEmitFromProp(int channel, int prop, int fadeTicks) {
    stage_.aiSounds_->EmitFromProp(channel, prop, fadeTicks);
}

void Stage::AiBridge::SfxEmitFromPad(int channel, int pad, int fadeTicks) {
    if (stage_.setup_) {
        if (const Pad* found = stage_.setup_->FindPad(pad)) {
            stage_.aiSounds_->EmitFromPad(channel, found->position, fadeTicks);
        }
    }
}

int Stage::AiBridge::SfxChannelVolume(int channel) const {
    return stage_.aiSounds_->Volume(channel);
}

void Stage::AiBridge::MusicPlaySlot(int slot, int stoppedSeconds, int totalSeconds) {
    stage_.music_.PlaySlot(slot, stoppedSeconds, totalSeconds);
}

void Stage::AiBridge::MusicStopSlot(int slot) {
    stage_.music_.StopSlot(slot);
}

void Stage::AiBridge::UseObject(int prop) {
    stage_.UseObject(prop);
}

void Stage::AiBridge::DestroyObject(PlacedObject& object) {
    if (object.prop < 0 || Objects::DestroyedLevel(object) != 0) {
        return;
    }
    // The damage that takes the object to destroyed, as a remote mine's.
    constexpr int kRemoteMine = 0x1D;
    const float damage = ((object.damage - object.maxDamage) + 1.0f) / 250.0f;
    stage_.damage_.Apply(object, damage, object.runtimePosition, kRemoteMine, -1);
}

void Stage::AiBridge::CollectProp(int prop) {
    stage_.CollectProp(prop);
}

void Stage::AiBridge::EquipObject(int prop, Chr& chr) {
    // TODO(https://github.com/liz-baker/frigate/issues/224): objDetach (or the
    // deregister, delist and disable of an unattached object) followed by
    // chrEquipWeapon or chrpropReparent.
    (void)prop;
    (void)chr;
}

void Stage::AiBridge::MoveObjectToPad(PlacedObject& object, int padId) {
    const StageSetup* setup = stage_.setup_ ? &*stage_.setup_ : nullptr;
    if (setup == nullptr || padId < 0) {
        return;
    }
    const Pad* pad = nullptr;
    if (padId < 10000) {
        pad = static_cast<size_t>(padId) < setup->Pads().size() ? &setup->Pads()[static_cast<size_t>(padId)] : nullptr;
    } else {
        const size_t index = static_cast<size_t>(padId - 10000);
        pad = index < setup->BoundPads().size() ? &setup->BoundPads()[index].pad : nullptr;
    }
    if (pad != nullptr) {
        stage_.objects_.MoveToPadPlacement(&object, *pad);
    }
}

void Stage::AiBridge::SetMonitorImage(PlacedObject& monitor, int slot, int bank) {
    // AI_TvChangeScreenBank: monitorSetImageByNum on the monitor's screen, or
    // on one of a multi-monitor's four.
    std::vector<MonitorScreen>* screens = stage_.MonitorScreensOf(monitor);
    if (screens == nullptr) {
        return;
    }
    if (monitor.type == static_cast<uint8_t>(PropDefType::Monitor)) {
        screens->front().StartList(stage_.monitorLists_->ListFor(bank));
    } else if (monitor.type == static_cast<uint8_t>(PropDefType::MultiMonitor) && slot >= 0 && slot < 4) {
        (*screens)[static_cast<size_t>(slot)].StartList(stage_.monitorLists_->ListFor(bank));
    }
}

void Stage::AiBridge::LaunchObject(PlacedObject& object) {
    // AI_ObjectRocketLaunch: the object takes off, nose first and still gaining speed.
    if (object.prop < 0) {
        return;
    }
    Objects& objects = stage_.objects_;
    objects.GiveProjectile(object.prop);
    if ((object.runtimeFlags & ObjectRuntime::HasProjectile) != 0 && object.projectile >= 0) {
        Projectile& projectile = objects.ProjectileAt(object.projectile);
        projectile.flags |= ProjectileFlag::Airborne | ProjectileFlag::NoSpin | ProjectileFlag::Falling;
        objects.SetSticky(object.prop);
        projectile.spin = IdentityMtxf();
        projectile.speed = { 0.0f, 0.016666666f, 0.0f };
        projectile.acceleration = { 0.0f, 0.29166666f, 0.0f };
    }
}

ChrHost Stage::AiBridge::HostOf(Chr& chr) const {
    ChrHost host;
    host.prop = chr.prop;
    host.model = chr.model;
    host.weaponsHeld = &chr.weaponsHeld;
    host.hat = &chr.hat;
    return host;
}

bool Stage::AiBridge::GiveWeapon(Chr& chr, int model, int item, uint32_t flags) {
    return stage_.objects_.GiveWeapon(HostOf(chr), chr.chrnum, model, item, flags);
}

bool Stage::AiBridge::GiveHat(Chr& chr, int model, uint32_t flags) {
    return stage_.objects_.GiveHat(HostOf(chr), model, flags);
}

void Stage::AiBridge::CloneChr(Chr& source, Chr& clone) {
    // The clone holds copies of its original's weapons, paired as theirs are,
    // and a copy of its hat.
    Objects& objects = stage_.objects_;
    const ChrHost cloneHost = HostOf(clone);
    int rightItem = -1;
    int leftItem = -1;
    int rightModel = 0;
    int leftModel = 0;
    if (const PlacedObject* right = source.weaponsHeld[0] >= 0 ? objects.ForProp(source.weaponsHeld[0]) : nullptr) {
        rightItem = right->weaponNum;
        rightModel = right->model;
    }
    if (const PlacedObject* left = source.weaponsHeld[1] >= 0 ? objects.ForProp(source.weaponsHeld[1]) : nullptr) {
        leftItem = left->weaponNum;
        leftModel = left->model;
    }
    if (rightItem >= 0) {
        objects.GiveWeapon(cloneHost, clone.chrnum, rightModel, rightItem, 0);
    }
    if (leftItem >= 0) {
        objects.GiveWeapon(cloneHost, clone.chrnum, leftModel, leftItem, 0x10000000);
    }
    // A pair stays a pair.
    if (source.weaponsHeld[0] >= 0 && source.weaponsHeld[1] >= 0 && clone.weaponsHeld[0] >= 0 &&
        clone.weaponsHeld[1] >= 0 && objects.Paired(source.weaponsHeld[0], source.weaponsHeld[1])) {
        objects.SetDualProps(clone.weaponsHeld[1], clone.weaponsHeld[0]);
    }
    if (source.hat >= 0) {
        if (const PlacedObject* hat = objects.ForProp(source.hat)) {
            objects.GiveHat(cloneHost, hat->model, 0);
        }
    }
}

void Stage::AiBridge::RemoveHeldItem(Chr& chr, int hand) {
    // chrSetWeaponFlag4.
    if (hand < 0 || hand > 1 || chr.weaponsHeld[static_cast<size_t>(hand)] < 0) {
        return;
    }
    if (PlacedObject* object = stage_.objects_.ForProp(chr.weaponsHeld[static_cast<size_t>(hand)])) {
        object->runtimeFlags |= ObjectRuntime::Remove;
    }
}

int Stage::AiBridge::HeldGunItem(const Chr& chr, int hand) const {
    // chrGetEquippedWeaponPropWithCheck.
    if (hand < 0 || hand > 1 || chr.weaponsHeld[static_cast<size_t>(hand)] < 0) {
        return -1;
    }
    const PlacedObject* object = stage_.objects_.ForProp(chr.weaponsHeld[static_cast<size_t>(hand)]);
    if (object == nullptr || !WeaponHasFlag(object->weaponNum, WeaponFlag::HoldAsGun)) {
        return -1;
    }
    return object->weaponNum;
}

} // namespace Frigate::Level
