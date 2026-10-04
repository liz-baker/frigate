#pragma once

#include "level/ChrAi.h"
#include "level/Stage.h"

namespace Frigate::Level {

// What the AI interpreter reaches the stage through: the functions of the
// decomp's player, HUD, camera, sound and object code the AI commands call.
class Stage::AiBridge final : public AiHost {
  public:
    explicit AiBridge(Stage& stage) : stage_(stage) {
    }

    AiPlayer Player() const override;
    void SetPlayerCollides(bool collides) override;
    int Difficulty() const override;
    int StageNumber() const override;
    float StageSeconds() const override;
    int Frame() const override;
    float PowerOnSeconds() const override;
    int PlayerCount() const override;
    float FarFogIntensitySquared() const override;
    bool RoomRendered(int room) const override;
    bool RoomsSharePortal(int roomA, int roomB) const override;
    bool PositionOffScreen(const Vec3& position, int tile) const override;
    bool PlayerAimsAtSphere(const Vec3& centre, float radius) const override;

    void AlarmActivate() override;
    void AlarmDeactivate() override;
    bool AlarmActive() const override;
    bool GasLeaking() const override;
    void GasLeak() override;
    int CiviliansKilled() const override;

    int ObjectiveCount() const override;
    bool ObjectiveComplete(int objective) const override;
    bool AllObjectivesComplete() const override;

    int PlayerWeapon(int hand) const override;
    int PlayerAmmo(int item) const override;
    void PlayerEquip(int hand, int item) override;
    void PlayerUnequip(int hand, int item) override;
    void PlayerRemoveItemInHand(int hand) override;
    bool PlayerHasProp(int prop) const override;
    bool WeaponHasFlag(int item, uint32_t flag) const override;

    void ShowBottomText(int text) override;
    void ShowTopText(int text) override;
    void BondControl(bool enable, uint8_t flags) override;
    void CountdownShow(bool show) override;
    void CountdownSet(float ticks) override;
    void CountdownRunning(bool running) override;
    bool CountdownIsRunning() const override;
    float CountdownValue() const override;

    int CameraMode() const override;
    void CameraReturnToBond() override;
    void CameraLookAtBondFromPad(int pad) override;
    void CameraSwitch(const PropDef& cameraPosition, int lookAtBond, int unused) override;
    void CameraOrbitPad(int pad, int latDistance, int vertDistance, int orbitSpeed60, int yOffset,
                        int initialRotation) override;
    void ScreenFade(bool toBlack) override;
    bool ScreenFadeCompleted() const override;
    void BondForceMove(float x, float z) override;
    void SwitchSky() override;
    void ExplodeAroundBond() override;
    int FolderActor() const override;
    void EndLevel() override;
    int StopTime() const override;
    void SetStopTime(int state) override;
    int Credits() const override;
    void SetCredits(int state) override;
    bool PlayerInvincible() const override;
    void SetPlayerInvincible(bool invincible) override;
    void SetBondKilledInAction() override;

    void SfxPlay(int channel, int sound) override;
    void SfxStop(int channel) override;
    void SfxSetVolume(int channel, int volume, int fadeTicks) override;
    void SfxFadeVolume(int channel, float volume, int fadeTicks) override;
    void SfxEmitFromProp(int channel, int prop, int fadeTicks) override;
    void SfxEmitFromPad(int channel, int pad, int fadeTicks) override;
    int SfxChannelVolume(int channel) const override;
    void MusicPlaySlot(int slot, int stoppedSeconds, int totalSeconds) override;
    void MusicStopSlot(int slot) override;

    void UseObject(int prop) override;
    void DestroyObject(PlacedObject& object) override;
    void DropProp(int prop, int how) override;
    void DropConcealedItems(Chr& chr) override;
    void CollectProp(int prop) override;
    void EquipObject(int prop, Chr& chr) override;
    void MoveObjectToPad(PlacedObject& object, int pad) override;
    bool KeyDropped(int key) const override;
    void SetMonitorImage(PlacedObject& monitor, int slot, int bank) override;
    void LaunchObject(PlacedObject& object) override;

    bool GiveWeapon(Chr& chr, int model, int item, uint32_t flags) override;
    bool GiveHat(Chr& chr, int model, uint32_t flags) override;
    bool DropNewItem(Chr& chr, int model, int item) override;
    void CloneChr(Chr& source, Chr& clone) override;
    void RemoveHeldItem(Chr& chr, int hand) override;
    int HeldGunItem(const Chr& chr, int hand) const override;

    const WeaponTable* Weapons() const override;
    float AiAccuracy() const override;
    float AiDamage() const override;
    float AiHealth() const override;
    float AiReactionSpeed() const override;
    float Ai007HealthMod() const override;
    float Ai007DamageMod() const override;
    float Ai007AccuracyMod() const override;
    float Ai007ReactionSpeed() const override;
    float PlayerDuckingHeight() const override;
    const Mtxf& ViewToWorld() const override;
    const Mtxf& PreviousViewToWorld() const override;
    bool PlayerDamageShowing() const override;
    void DamagePlayer(float damage, float angle) override;
    void CountPlayerShot(int item, int shotRegister) override;
    void PropHitSound(int item, const Vec3& position, bool fleshy) override;
    void WorldHitSound(int item, const Vec3& position) override;
    void CreateSpark(const Vec3& position, int type, float size, int room) override;
    void ShootObject(PlacedObject& object, float damage, const Vec3& position, int item) override;
    void LaunchRound(Chr& chr, int item, const Vec3& origin, int tile, const Vec3& direction, float elevation,
                     float heading) override;
    void CountKill() override;
    void CountCivilianCasualty() override;

  private:
    // The character-side fields the object code holds items on.
    ChrHost HostOf(Chr& chr) const;

    Stage& stage_;
};

} // namespace Frigate::Level
