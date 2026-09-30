#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "level/BgLevel.h"
#include "level/BgRender.h"
#include "level/BgVisibility.h"
#include "level/Chrs.h"
#include "level/Doors.h"
#include "level/Fog.h"
#include "level/GunDraw.h"
#include "level/Hud.h"
#include "level/LightFixtures.h"
#include "level/IntroSection.h"
#include "level/Lang.h"
#include "level/Objects.h"
#include "level/PlayerCamera.h"
#include "level/PlayerMove.h"
#include "level/PlayerWeapons.h"
#include "level/RoomLoad.h"
#include "level/Sky.h"
#include "level/StageSetup.h"
#include "level/Watch.h"

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
};

// A loaded stage and the frame loop that runs it: the part of lvlStageLoad,
// lvlTick and lvlRender a single-player attract-mode demo reaches.
class Stage {
  public:
    static constexpr int kMaxPlayers = 4;
    // getPlayerCount(): the demos are single-player.
    static constexpr int kPlayerCount = 1;

    // `hudSource` loads the HUD's fonts and images; without one the HUD draws
    // no text, crosshair or icons.
    explicit Stage(BgSource& source, HudSource* hudSource = nullptr);
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    // lvlStageLoad. False if the stage's data can't be loaded; Error() says why.
    // `demoSlot` is check_ramrom_flags(): the slot of the demo being played, 0
    // outside a demo. It picks which of the setup's spawn and item records
    // apply. `difficulty` is the selected difficulty (0 to 2), which decides
    // which of its objects load.
    bool Load(LevelId stage, int32_t demoSlot = 0, int difficulty = 0);
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
    // bondviewSetCameraMode(CAMERAMODE_SWIRL)'s entry: the stage's fog and sky
    // environment loads (and with it the z range the projection is built from).
    // The intro camera's own cinema environment and the swirl's fade are not
    // ported.
    // TODO(https://github.com/liz-baker/frigate/issues/140): those, with the
    // intro camera.
    void EnterSwirl();
    // bondviewSetCameraMode(CAMERAMODE_FP)'s entry: the player equips the
    // intro's starting weapons.
    void EnterFirstPerson();
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
    // The text the level shows (langGet).
    const Lang& GetLang() const {
        return lang_;
    }
    // bondarmour.
    float Armour() const {
        return armour_;
    }

    // g_CurrentStageToLoad.
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
    }
    bool ControlsLocked() const {
        return controlsLocked_;
    }
    const PlayerStats& Stats(int player) const {
        return stats_[player];
    }

  private:
    // langInit's text banks and the stage's own.
    void LoadText(LevelId stage);
    // proplvreset2's share of loading the stage's setup file.
    bool LoadSetup(LevelId stage, int difficulty);
    // What the gun code reads off the player this frame.
    GunFrame MakeGunFrame(const LookAt* lookAt) const;
    // The view rectangle and aspect bondviewMovePlayerUpdateViewport sets from
    // the screen setting and how far the watch is up.
    void UpdateViewport();
    // propsTickPlayer's pass over the objects: whatever the player stands close
    // enough to is picked up.
    void TickPickups();
    // What skyRender reads off the camera and the stage.
    SkyView MakeSkyView() const;
    // What lvlRender's HUD pass (maybe_mp_interface) reads.
    HudFrame MakeHudFrame(const PlayerPose& pose) const;

    BgSource& source_;
    BgLevel level_;
    BgVisibility visibility_;
    // Constructed before render_, which keeps a reference to it
    // (chrpropsRenderPass's props).
    Objects objects_;
    Chrs chrs_;
    Doors doors_;
    Fog fog_;
    Sky sky_;
    LightFixtures lightFixtures_;
    RoomLoad roomLoad_;
    BgRender render_;
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
    GunDraw gunDraw_;
    Hud hud_;
    Watch watch_;
    Lang lang_;
    // bondarmour: the body armour the player has, 0 to 1.
    // TODO(https://github.com/liz-baker/frigate/issues/146): health, and the
    // damage that takes both.
    float armour_ = 0.0f;
    int difficulty_ = 0;
    HudSource* hudSource_ = nullptr;
    PlayerOptions options_;
    LevelId currentStage_ = LevelId::Max;
    bool loaded_ = false;
    // D_800483C0: the level's draw switch, which the portal debug toggles.
    bool drawEnabled_ = true;
    bool controlsLocked_ = false;
    int speedFrames_ = 1;
    // The aspect of the view the frame draws with.
    float aspect_ = 4.0f / 3.0f;
    std::string error_;
};

} // namespace Frigate::Level
