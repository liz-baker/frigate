#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "audio/SoundPlayer.h"
#include "game/AiSoundChannels.h"
#include "game/MissionMusic.h"
#include "game/Sliders007.h"
#include "game/menu/BloodAnimation.h"
#include "level/AiLists.h"
#include "level/AutoAim.h"
#include "level/BgHit.h"
#include "level/BgLevel.h"
#include "level/BgRender.h"
#include "level/BgVisibility.h"
#include "level/CheatInput.h"
#include "level/Cheats.h"
#include "level/ChrActions.h"
#include "level/ChrAi.h"
#include "level/ColourScreen.h"
#include "level/Chrs.h"
#include "level/Doors.h"
#include "level/Fog.h"
#include "level/GunDraw.h"
#include "level/Casings.h"
#include "level/EffectsDraw.h"
#include "level/Hud.h"
#include "level/Interact.h"
#include "level/LightFixtures.h"
#include "level/IntroSection.h"
#include "level/Lang.h"
#include "level/ObjectDamage.h"
#include "level/Objectives.h"
#include "level/Objects.h"
#include "level/PlayerCamera.h"
#include "level/PlayerHealth.h"
#include "level/PlayerMove.h"
#include "level/PlayerWeapons.h"
#include "level/PropLinks.h"
#include "level/RoomLoad.h"
#include "level/GunLauncher.h"
#include "level/ObjectDeform.h"
#include "level/Projectiles.h"
#include "level/Shots.h"
#include "level/Sky.h"
#include "level/StageResult.h"
#include "level/StageSetup.h"
#include "level/Vehicles.h"
#include "level/Waypoints.h"
#include "level/Watch.h"
#include "model/Random.h"

namespace Ship {
class ResourceManager;
} // namespace Ship

namespace Frigate::Level {

// The stage's frame accounting (g_ClockTimer, g_GlobalTimerDelta,
// g_GlobalTimer, g_StageTimeSec).
struct StageClock {
    int clockTimer = 1;
    float globalTimerDelta = 1.0f;
    int globalTimer = 0;
    // D_800483B0: clock ticks since the stage loaded.
    int stageTicks = 0;
    float stageTimeSec = 0.0f;
    // D_80048380: the frames run while the controls were not locked.
    int frame = 0;
};

// What a stage load resets in each player's g_playerPlayerData.
struct PlayerStats {
    int autoaim = 0;
    int sight = 0;
    float handicap = 1.0f;
    float perspectiveHeight = 1.0f;
    int timeOtherPlayersOnScreen = 0;
    int damageToBackside = 0;
    int32_t minTimeBetweenKills = INT32_MAX;
    int maxTimeBetweenKills = 0;
    int mostKilledOneLife = 0;
    int mostKilledOneTime = 0;
    int longestInning = 0;
    int32_t shortestInning = INT32_MAX;
    int orderOutInYolt = 0;
    int flagCounter = 0;
    float distanceTraveled = 0.0f;
    float bodyArmorPickups = 0.0f;
    std::array<int, 4> killCounts{};
    // kill_count: the characters that died.
    int killCount = 0;
    // shot_count, by SHOT_REGISTER: the shots fired and what they hit.
    std::array<int, 7> shotCount{};
};

// lvlSetMultipliersForDifficulty's results: the enemy and ammo scales of the
// difficulty being played.
struct DifficultyModifiers {
    // F_80030B14, F_80030B18 and F_80030B24.
    float cctvAlarmScale = 1.0f;
    float unnamed0x80030B18 = 1.0f;
    float unnamed0x80030B24 = 1.0f;
    // g_AutogunPendingDamageTick and g_AutogunDamageScalar.
    float autogunPendingDamageTick = 1.0f;
    float autogunDamageScalar = 1.0f;
    float aiAccuracy = 1.0f;
    float aiDamage = 1.0f;
    float aiHealth = 1.0f;
    float explosionDamage = 1.0f;
    // `difficulty`.
    float difficulty = 1.0f;
    float soloAmmo = 1.0f;
    float aiReactionSpeed = 1.0f;
};

// A loaded stage and the frame loop that runs it: the part of lvlStageLoad,
// lvlTick and lvlRender a single-player attract-mode demo reaches.
class Stage {
  public:
    static constexpr int kMaxPlayers = 4;
    // getPlayerCount(): the demos are single-player.
    // TODO(https://github.com/liz-baker/frigate/issues/288): the multiplayer
    // tick calls BuildPlayerMoveInput (level/PlayerPadInput.h) per player.
    static constexpr int kPlayerCount = 1;

    // `hudSource` loads the HUD's fonts and images; without one the HUD draws
    // no text, crosshair or icons.
    explicit Stage(BgSource& source, HudSource* hudSource = nullptr);
    ~Stage();
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    // lvlStageLoad. False if the stage's data can't be loaded; Error() says why.
    // `demoSlot` is check_ramrom_flags(): the slot of the demo being played, 0
    // outside a demo. It picks which of the setup's spawn and item records
    // apply. `difficulty` is the selected difficulty (0 to 2), which decides
    // which of its objects load. `demo` says an attract demo is running; slot 0
    // is a valid demo slot, so it can't be inferred from `demoSlot`.
    bool Load(LevelId stage, int32_t demoSlot = 0, int difficulty = 0, bool demo = false);
    // The 007 options screen's sliders, which apply when the difficulty is 007.
    // Set before Load: the characters' health is scaled as they spawn.
    void SetSliders007(const Game::Sliders007& sliders) {
        sliders007_ = sliders;
    }
    // g_CheatActivated with g_AppendCheatSinglePlayer set: the cheats the cheat
    // menu switched on, as cheat ids. Set before Load; a stage with none runs
    // no cheats.
    void SetCheats(std::vector<int32_t> activated) {
        activatedCheats_ = std::move(activated);
    }
    // The cheats a stage keeps switched on.
    const Cheats& GetCheats() const {
        return cheats_;
    }
    // lvlUnloadStageTextData's share of the teardown.
    void Unload();

    // lvlTick, while a stage other than the title is running.
    void Tick();
    // lvlRender's single-player pass. `pose` is the camera's placement this
    // frame; `resourceManager` loads the props' models.
    Gfx* Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, const PlayerPose& pose);

    // lvlViewMoveTick's move for the player, one tick of `input`.
    void MovePlayer(const MoveInput& input);
    // The player's movement state.
    const PlayerMove& Player() const {
        return player_;
    }
    // bondviewSetCameraMode(CAMERAMODE_INTRO), which a single-player stage
    // starts with: the establishing shot, the fade to the swirl round Bond and
    // then first person. A demo (and a stage with no camera or swirl records)
    // skips ahead of whichever it lacks.
    void EnterIntro();
    // fogLoadLevelEnvironment: the stage's fog and sky environment (the
    // intro camera's cinema one when `cinema`), and with it the z range the
    // projection is built from.
    void LoadFogEnvironment(bool cinema);
    // The camera's placement this frame: the cinematic camera while a mode
    // places one, the player's own view otherwise.
    PlayerPose CameraPose() const;
    // ramromFadeToTitle: bondviewSetCameraMode(CAMERAMODE_FADE_TO_TITLE) fades
    // the view to black over 60 ticks; once it has, the game returns to the
    // title. Does nothing when the fade has already started.
    void FadeToTitle();
    bool FadeToTitleComplete() const;
    // The stage's fog and sky environment.
    const Fog& GetFog() const {
        return fog_;
    }
    // The stage's light fixtures, which shots put out.
    LightFixtures& GetLightFixtures() {
        return lightFixtures_;
    }
    // The weapons the player holds and carries; not Ready() for a source with
    // no weapon table.
    const PlayerWeapons& Weapons() const {
        return weapons_;
    }
    // The player's option settings (control style and so on).
    PlayerOptions& Options() {
        return options_;
    }
    // The HUD (crosshair, ammo counter, gauges, messages), and the watch.
    Hud& GetHud() {
        return hud_;
    }
    const Hud& GetHud() const {
        return hud_;
    }
    const Watch& GetWatch() const {
        return watch_;
    }
    // See WatchMenu::SetSettingsSaver.
    void SetSettingsSaver(std::function<void()> saver) {
        watch_.SetSettingsSaver(std::move(saver));
    }
    // The text the level shows (langGet).
    const Lang& GetLang() const {
        return lang_;
    }
    // The player's health and armour, and the colour wash over the view.
    const PlayerHealth& Health() const {
        return health_;
    }
    const ColourScreen& Colour() const {
        return colour_;
    }
    // How many lines the video interface moves the picture down this frame
    // (negative: up) while explosions shake it.
    // TODO(https://github.com/liz-baker/libultraship-wiiu/issues/69): nothing
    // presents the frame at this offset yet.
    int32_t ScreenShakeOffset() const {
        return screenShakeOffset_;
    }
    // alarmIsActive: a player switched an alarm on.
    bool AlarmActive() const {
        return alarmTimer_ > 0;
    }

    // The AI: the interpreter and its stage state, and whether a command asked
    // to end the level (aiEndLevel).
    ChrAi* Ai() {
        return ai_.get();
    }
    bool LevelEndRequested() const {
        return levelEndRequested_;
    }
    // Which of bossReturnTitleStage and bossRunTitleStage the stage has called,
    // if either.
    StageExit Exit() const {
        return exit_;
    }
    // What the stage leaves for the mission screens, read as it ends.
    StageResult Result() const;
    // mission_timer: the ticks the player has been in play.
    int MissionTimer() const {
        return missionTimer_;
    }
    // The enemy and ammo scales of the difficulty.
    const DifficultyModifiers& Modifiers() const {
        return modifiers_;
    }
    // The characters' waypoint graph.
    const Waypoints& Paths() const {
        return paths_;
    }
    // g_CurrentStageToLoad.
    Game::MissionMusic& Music() {
        return music_;
    }
    LevelId CurrentStage() const {
        return currentStage_;
    }
    const std::string& Error() const {
        return error_;
    }
    BgLevel& Bg() {
        return level_;
    }
    const BgLevel& Bg() const {
        return level_;
    }
    BgVisibility& Visibility() {
        return visibility_;
    }
    const PlayerCamera& Camera() const {
        return camera_;
    }
    PlayerCamera& Camera() {
        return camera_;
    }
    // g_CurrentSetup; empty for a stage with no setup file.
    const std::optional<StageSetup>& Setup() const {
        return setup_;
    }
    // The counts proplvreset2 sizes the guard and object tables with.
    const PropCounts& PropTotals() const {
        return propCounts_;
    }
    // The propdef records of the setup that proplvreset2 creates as objects on
    // this difficulty and player count, as indexes into Setup()->PropDefs().
    const std::vector<size_t>& ObjectDefs() const {
        return objectDefs_;
    }
    // The objects the stage created and the prop list they are in.
    const Objects& PlacedObjects() const {
        return objects_;
    }
    // The characters the stage spawned.
    const Chrs& PlacedChrs() const {
        return chrs_;
    }
    // The doors the stage created.
    const Doors& PlacedDoors() const {
        return doors_;
    }
    // How the setup's pads found their stan tiles.
    const StageSetup::StanResolution& PadTiles() const {
        return padTiles_;
    }
    // The first player's intro section state and where it starts.
    const IntroSetup& Intro() const {
        return intro_;
    }
    const PlayerSpawn& Spawn() const {
        return spawn_;
    }
    const StageClock& Clock() const {
        return clock_;
    }
    // speedgraphframes: the frames since the last, from which lvlManageMpGame
    // works out g_ClockTimer, and g_GlobalTimerDelta alongside it, which are
    // both 0 while the controls are locked (the watch is open).
    void SetClockTimer(int ticks) {
        speedFrames_ = ticks;
        clock_.clockTimer = controlsLocked_ ? 0 : ticks;
        clock_.globalTimerDelta = static_cast<float>(clock_.clockTimer);
        clock_.globalTimer += clock_.clockTimer;
        if (!controlsLocked_) {
            clock_.frame++;
        }
        projectiles_.SetClock({ clock_.clockTimer, clock_.globalTimerDelta, clock_.globalTimer, clock_.frame });
    }
    bool ControlsLocked() const {
        return controlsLocked_;
    }
    // joyGetButtonsPressedThisFrame(PLAYER_1, ANY_BUTTON): the buttons that went
    // down this frame, which lvlTick's idle check reads.
    void SetButtonsPressed(uint16_t pressed) {
        buttonsPressed_ = pressed;
    }
    const PlayerStats& Stats(int player) const {
        return stats_[player];
    }

  private:
    // The screens of a monitor object, made on first use from the image numbers
    // its setup gave them (setupSingleMonitor and setupMultiMonitor); empty when
    // the animation lists did not load.
    std::vector<MonitorScreen>* MonitorScreensOf(PlacedObject& object);
    // process_monitor_animation_microcode's state update, for the screens of
    // `object` that `screens` selects.
    void TickMonitor(PlacedObject& object, uint32_t screens);
    // monAnim33BlackSolid on every screen of a destroyed monitor.
    void BlackenMonitor(PlacedObject& object);
    // objTick's CCTV branch: the camera swings, and raises the alarm when it
    // has had the player in sight long enough.
    void TickCctv(PlacedObject& object);
    // objTick's autogun branch: the gun turns to the player it sees, tracks and
    // spins its barrels up.
    void TickAutogun(PlacedObject& object);
    // The tail of objTick for an autogun, after its pose: the muzzle flash,
    // the bullet trace and the damage to the player.
    void FireAutogun(PlacedObject& object);

    class AiBridge;
    class GunBridge;
    class TankBridge;
    class WatchBridge;
    // langInit's text banks and the stage's own.
    void LoadText(LevelId stage);
    // proplvreset2's share of loading the stage's setup file.
    bool LoadSetup(LevelId stage, int difficulty);
    // What the gun code reads off the player this frame.
    GunFrame MakeGunFrame(const LookAt* lookAt);
    // The view rectangle and aspect bondviewMovePlayerUpdateViewport sets from
    // the screen setting and how far the watch is up.
    void UpdateViewport();
    // propsTickPlayer's pass over the objects: whatever the player stands close
    // enough to is picked up.
    void TickPickups();
    // handle_alarm_gas_timer_calldamage, which ends propsTick: the alarm, the
    // toxic gas and the countdown.
    void TickAlarm();
    // alarmActivate and alarmDeactivate (which stops the alarm's sound).
    void ActivateAlarm();
    void DeactivateAlarm();
    // init_trigger_toxic_gas_effect.
    void TriggerGas(const Vec3& source);
    // deactivate_alarm_sound_effect and check_deactivate_gas_sound.
    void StopAlarmSound();
    void StopGasSound();
    // bondviewPlayerStopAudioForPause: the sounds that only play while the
    // controls are free stop while they are locked.
    void StopAudioForPause();
    // lvlSetMultipliersForDifficulty.
    void SetDifficultyModifiers();
    // The loop lvlStageLoad (the enemy rockets) and lvlManageMpGame (the rest)
    // run over g_CheatActivated, and cheatButtonHandleCheatsTurnedOn for each.
    void TurnOnActivatedCheats(bool enemyRockets);
    void TurnOnCheat(int32_t cheat);
    // cheatButtonHandleCheatsTurnedOff.
    void TurnOffCheat(int32_t cheat);
    // cheat_buttons_mp_related: the buttons of this tick go into the code
    // buffer, and a code that completes switches its cheat on or off.
    void TickCheatInput(const MoveInput& input);
    // HUDMESSAGEBOTTOM(langGet(getStringID(LMISC, slot))).
    void ShowMiscMessage(int slot);
    // Wires the shots' effects to the rest of the stage, once.
    void AttachEffects();
    // alloc_explosion_smoke_casing_scorch_impact_buffers and
    // alloc_shattered_window_pieces, with the room details the effects read.
    void ResetEffects();
    // lvlRender's draws of the effects, after the level: the tracers of the
    // characters, the sparks, the shattered glass and the debris.
    Gfx* RenderEffects(Gfx* dl);
    // chrpropTick.
    void TickProps();
    // record_damage_kills for the single player, from the direction
    // (`vectorX`, `vectorZ`) (bondviewCallRecordDamageKills with no other player
    // to credit).
    void RecordPlayerDamage(float damage, float vectorX, float vectorZ, bool affectsArmour);
    // joyRumblePakStart for the player's controller, and for the second one
    // the two-controller control styles use. A demo's playback has no rumble.
    void RumblePlayer(float duration);
    // bondviewKillCurrentPlayer.
    void KillPlayer();
    // bossReturnTitleStage and bossRunTitleStage; the first call stands.
    void ReturnTitleStage();
    void RunTitleStage();
    // bondviewSetCameraMode(CAMERAMODE_DEATH_CAM_SP) and (..._MP): a replay of
    // the death from a new angle, and the fade out that ends it.
    void EnterDeathCamera();
    void EnterDeathCameraFade();
    bool DeathCameraShowing() const {
        return cameraMode_ == CameraMode::DeathCamSinglePlayer || cameraMode_ == CameraMode::DeathCamMultiPlayer;
    }
    // bondviewMovePlayerUpdateViewport's tail: the exits a held-still stage
    // (the mission over, or the player dead) waits for a button to take.
    void TickExits(const MoveInput& input);
    // What the damage code reads each time it runs.
    HealthFrame MakeHealthFrame() const;
    // What the objectives' criteria read of the level.
    ObjectiveWorld MakeObjectiveWorld() const;
    // display_objective_status_text_on_status_change.
    void TickObjectiveText();
    // The dying player's share of maybe_mp_interface: the blood, the colour
    // wash and the hand-over to the death camera.
    void TickPlayerDeath(const std::shared_ptr<Ship::ResourceManager>& resourceManager);
    // bondviewPlayerTickExplode.
    void TickPlayerExplode();
    // bondviewPlayerTickDamageAndHealth.
    void TickPlayerDamage();
    // propobjSetDropped and objDrop: an item a character holds is set to fall
    // as `dropType`, and falls when the character's tick next drops what it
    // holds.
    void SetDropped(int prop, int dropType);
    void DropObject(int prop);
    // The character that holds the item lets go of it (objDetach).
    void ReleaseHeld(int prop);
    // sub_GAME_7F03D058: takes a prop out of the collision tests (`collides`
    // false) or puts it back.
    void SetPropCollides(int prop, bool collides);
    // The AI state a stage load resets (reinit_between_menus).
    void ResetAiState();
    // bond_interact_object: the player's use button on the prop propFindForInteract
    // picks. True when there was nothing to use.
    bool UseProp();
    // propobjInteract for an object.
    void UseObject(int prop);
    // propPickupByPlayer's caller and propExecuteTickOperation: collects the
    // pickup of `prop` and takes it out of the world as the pickup says.
    void CollectProp(int prop);
    InteractScene MakeInteractScene();
    InteractPlayer MakeInteractPlayer() const;
    // What skyRender reads off the camera and the stage.
    SkyView MakeSkyView() const;
    // What lvlRender's HUD pass (maybe_mp_interface) reads.
    HudFrame MakeHudFrame(const PlayerPose& pose) const;

    // The camera a cinematic mode places (bondviewFrozenCameraTick's outputs):
    // `position` looking at `lookAt`, with the stan tile `tile` and the point
    // `walkStart` the walk to the camera's tile starts from.
    struct FrozenCamera {
        Vec3 position{};
        Vec3 lookAt{};
        Vec3 up{ 0.0f, 1.0f, 0.0f };
        int tile = kNoTile;
        Vec3 walkStart{};
    };
    // bondviewSetCameraMode and bondviewAdvanceCameraMode, for the modes a
    // single-player stage runs through at its start.
    void SetCameraMode(CameraMode mode);
    void AdvanceCameraMode();
    // bondviewFrozenCameraTick for the intro, fade and swirl modes.
    FrozenCamera FrozenCameraTick(const MoveInput& input);
    // bondviewFrozenCameraTick for the death cameras.
    FrozenCamera DeathCameraTick(uint16_t newButtons);
    // bondviewSetCurrentPlayerPosition: places the cinematic camera.
    void SetFrozenCamera(const FrozenCamera& camera);
    // solo_char_load for the single player's new body, and
    // bondviewRemovePlayerBody: the character of Bond the swirl shows.
    void LoadBondBody();
    void RemoveBondBody();
    float BondBodyYaw() const;
    // setsuboffset and setsubroty of Bond's body, from the player's prop.
    void PlaceBondBody();
    // The ACT_BONDDIE replay of the death animation.
    void StartBondDeathAnimation();
    // currentPlayerStartChrFade: Bond's body (while he has one) fades to
    // `targetFrac` over `duration` ticks.
    void StartBodyFade(float duration, float targetFrac);

    BgSource& source_;
    BgLevel level_;
    BgVisibility visibility_;
    // Constructed before render_, which keeps a reference to it
    // (chrpropsRenderPass's props).
    Objects objects_;
    Chrs chrs_;
    Doors doors_;
    PropLinks links_;
    Objectives objectives_;
    Fog fog_;
    Sky sky_;
    LightFixtures lightFixtures_;
    BgHit bgHit_;
    TextureHits textureHits_;
    RoomLoad roomLoad_;
    BgRender render_;
    // The shots' effects: the sparks, the marks and scorches, the broken
    // glass, the explosions and smoke, and the damage objects take.
    BulletSparks sparks_;
    BulletImpacts impacts_;
    GlassShards shards_;
    Explosions explosions_;
    EffectImages effectImages_;
    ObjectDamage damage_;
    ObjectDeform deform_;
    Shots shots_;
    Projectiles projectiles_;
    GunLauncher launcher_;
    EffectsDraw effects_;
    PlayerCamera camera_;
    StageClock clock_;
    std::array<PlayerStats, kMaxPlayers> stats_{};
    std::optional<StageSetup> setup_;
    StageSetup::StanResolution padTiles_;
    PropCounts propCounts_;
    std::vector<size_t> objectDefs_;
    IntroSetup intro_;
    PlayerSpawn spawn_;
    // Bond's head, created once and kept for every life.
    std::unique_ptr<HeadMotion> head_;
    PlayerMove player_;
    PlayerWeapons weapons_;
    Casings casings_;
    GunDraw gunDraw_;
    std::unique_ptr<GunBridge> gunBridge_;
    std::unique_ptr<TankBridge> tankBridge_;
    std::unique_ptr<WatchBridge> watchBridge_;
    Hud hud_;
    Watch watch_;
    Lang lang_;
    // bondhealth, bondarmour and the state of the damage display and of death.
    PlayerHealth health_;
    // colourscreenred, ... and the body fade.
    ColourScreen colour_;
    // g_bondviewForceDisarm.
    int forceDisarm_ = 0;
    // killed_civilians.
    int civilianCasualties_ = 0;
    // The blood frames of a dying player's view, once he first dies, and
    // whether the current frame draws one.
    std::optional<Game::Menu::BloodAnimation> blood_;
    bool bloodOverlay_ = false;
    // currentPlayerGetYAutoAimEnabled and ...XAutoAimEnabled (the player's
    // auto-aim option).
    bool autoAimEnabled_ = true;
    AutoAim autoAim_;
    // g_SurroundBondWithExplosionsFlag, g_SurroundBondWithExplosionsTicks and
    // g_PlayerTickExplodeCreatePosition.
    bool surroundBondWithExplosions_ = false;
    int surroundTicks_ = 0;
    int tickExplodePosition_ = 0;
    // field_2A08: the pitch the sight points at, in radians, which the tank's
    // gun follows.
    float sightPitch_ = 0.0f;
    int32_t screenShakeOffset_ = 0;
    // lifestarttime60: the stage tick the current life began on.
    int lifeStartTicks_ = 0;
    // alarm_timer.
    int alarmTimer_ = 0;
    // The toxic gas (activate_gas_sound_timer, toxic_gas_sound_timer,
    // gasTimeToFullOpacity, gasDoesDamageFlag and D_80030ADC) and the
    // mission countdown (clock_time, clock_enable and clock_drawn_flag) the AI
    // starts.
    bool gasActivate_ = false;
    float gasTimer_ = 0.0f;
    float gasFullOpacityTime_ = 3600.0f;
    bool gasDamages_ = true;
    int gasLastCough_ = 0;
    // gasLeakTimer and gasLeakSource: how long the gas has hissed and where it
    // leaks from; ptr_gas_sound and ptr_alarm_sfx: the hiss and the alarm.
    float gasLeakTimer_ = 0.0f;
    Vec3 gasLeakSource_{};
    AudioDriver::SoundState* gasSound_ = nullptr;
    AudioDriver::SoundState* alarmSound_ = nullptr;
    float countdownTime_ = 0.0f;
    bool countdownRunning_ = false;
    int countdownHidden_ = 1;
    // D_80048394 and D_800483A8: the ticks lvlGetCurrentMultiPlayerSec and
    // lvlGetCurrentMultiPlayerMin count.
    int mpTicks_ = 0;
    int powerTicks_ = 0;
    // D_80048390, D_80048388 and D_8004838C: the ticks since a button went down,
    // whether that has been 30 seconds, and the ticks spent idle since.
    int idleTicks_ = 0;
    bool idle_ = false;
    int idleTotal_ = 0;
    uint16_t buttonsPressed_ = 0;
    // The camera mode (g_CameraMode) as the AI and the pose set it, and
    // is_timer_active, stop_time_flag, credits_state, g_PlayerInvincible and
    // g_isBondKIA.
    CameraMode cameraMode_ = CameraMode::None;
    // g_CameraAfterCinema: the mode the camera moves on from at the end of the
    // tick; camera_transition_timer and intro_camera_index, the time and
    // swirl record the cinematic camera is at; camera_fade_active.
    CameraMode cameraAfterCinema_ = CameraMode::None;
    float cameraTransitionTimer_ = 0.0f;
    size_t introCameraIndex_ = 0;
    bool cameraFadeActive_ = false;
    // The cinematic camera (cameramode == 1): pos, pos2, offset, pos3 and
    // cameratile of `struct player`.
    bool frozenCamera_ = false;
    Vec3 cameraPos_{};
    Vec3 cameraPos2_{};
    Vec3 cameraUp_{ 0.0f, 1.0f, 0.0f };
    Vec3 cameraFloorPos_{};
    int cameraTile_ = kNoTile;
    int cameraRoom_ = 1;
    // The death camera: camera_mode (the replays so far), flt_800799E8 (where
    // it stands) and dword_800799F4 (the prop it looks at, -1 for the player).
    // CAMERAMODE_SWIRL: the replay count at which the death camera is done.
    static constexpr int kDeathReplays = 3;
    struct DeathCameraState {
        int replays = 0;
        Vec3 spot{};
        int lookProp = -1;
    };
    DeathCameraState deathCamera_;
    // The exit cutscene's camera (cameramode 1 while CAMERAMODE_POSEND):
    // g_CameraLookAtBondPad, gBondViewCutscene and its look-at flag, or the
    // orbit round a pad (flt_80079A00 to dword_80079A14).
    const Pad* cutscenePad_ = nullptr;
    const PropDef* cutscene_ = nullptr;
    bool cutsceneLooksAtBond_ = false;
    struct Orbit {
        float angle = 0.0f;
        float speed = 0.0f;
        float distance = 0.0f;
        float height = 0.0f;
        float targetHeight = 0.0f;
        int pad = 0;
    };
    Orbit orbit_;
    // The slot of Bond's body in chrs_ while he has one (the intro's swirl
    // shows it), -1 for none.
    int bondChr_ = -1;
    // g_ForceBondMoveOffset.
    Vec3 forcedMove_{};
    // check_ramrom_flags() != 0: a demo is playing.
    bool demo_ = false;
    bool timerActive_ = true;
    int stopTime_ = 0;
    int credits_ = 0;
    bool playerInvincible_ = false;
    // g_ContPlaybackFunc is set: an attract demo is playing back.
    bool demoPlayback_ = false;
    // cheatBondInvincible, g_VisibleToGuardsFlag and debug_fast_bond_flag: what
    // the invincibility, invisibility and turbo mode cheats set.
    bool cheatInvincible_ = false;
    bool visibleToGuards_ = true;
    bool fastBond_ = false;
    Cheats cheats_;
    CheatInput cheatInput_;
    // debug_VisCVG_flag: the line mode cheat draws the coverage over the frame.
    bool lineMode_ = false;
    std::vector<int32_t> activatedCheats_;
    bool bondKilledInAction_ = false;
    bool levelEndRequested_ = false;
    // The first exit called, and mission_failed_or_aborted.
    StageExit exit_ = StageExit::None;
    bool aborted_ = false;
    // mission_timer, and the buttons held at the last move (the player's
    // buttons_pressed field, which the next move compares against).
    int missionTimer_ = 0;
    uint16_t previousButtons_ = 0;
    DifficultyModifiers modifiers_;
    // The AI: the lists, the waypoint graph, the interpreter and what it
    // reaches the stage through.
    AiLists aiLists_;
    Waypoints paths_;
    std::unique_ptr<ChrActions> aiActions_;
    std::unique_ptr<AiBridge> aiBridge_;
    std::unique_ptr<ChrAi> ai_;
    // The stage's music (mp_music.c) and the sounds the AI plays on its
    // channels (sfx_related).
    Game::MissionMusic music_{ Frigate::RandomGetNext };
    std::unique_ptr<Game::AiSoundChannels> aiSounds_;
    std::shared_ptr<const MonitorLists> monitorLists_;
    std::unique_ptr<Vehicles> vehicles_;
    int difficulty_ = 0;
    Game::Sliders007 sliders007_;
    HudSource* hudSource_ = nullptr;
    PlayerOptions options_;
    LevelId currentStage_ = LevelId::Max;
    bool loaded_ = false;
    // D_800483C0: the level's draw switch, which the open watch turns off.
    bool drawEnabled_ = true;
    bool controlsLocked_ = false;
    // copiedgoldeneye: the key analyser has copied the GoldenEye key.
    bool copiedGoldenEye_ = false;
    int speedFrames_ = 1;
    // The aspect of the view the frame draws with.
    float aspect_ = 4.0f / 3.0f;
    std::string error_;
};

} // namespace Frigate::Level
