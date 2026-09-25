#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "level/BgLevel.h"
#include "level/BgRender.h"
#include "level/BgVisibility.h"
#include "level/IntroSection.h"
#include "level/PlayerCamera.h"
#include "level/PlayerMove.h"
#include "level/PlayerWeapons.h"
#include "level/StageSetup.h"

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

    explicit Stage(BgSource& source);
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    // lvlStageLoad. False if the stage's data can't be loaded; Error() says why.
    // `demoSlot` is check_ramrom_flags(): the slot of the demo being played, 0
    // outside a demo. It picks which of the setup's spawn and item records
    // apply.
    bool Load(LevelId stage, int32_t demoSlot = 0);
    // lvlUnloadStageTextData's share of the teardown.
    void Unload();

    // lvlTick, while a stage other than the title is running.
    void Tick();
    // lvlRender's single-player pass. `pose` is the camera's placement this
    // frame.
    Gfx* Render(Gfx* dl, const PlayerPose& pose);

    // lvlViewMoveTick's move for the player, one tick of `input`.
    void MovePlayer(const MoveInput& input);
    // The player's movement state.
    const PlayerMove& Player() const {
        return player_;
    }
    // bondviewSetCameraMode(CAMERAMODE_FP)'s entry: the player equips the
    // intro's starting weapons.
    void EnterFirstPerson();
    // The weapons the player holds and carries; not Ready() for a source with
    // no weapon table.
    const PlayerWeapons& Weapons() const {
        return weapons_;
    }
    // The player's option settings (control style and so on).
    PlayerOptions& Options() {
        return options_;
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
    // g_ClockTimer: the ticks the next Tick() accounts for.
    void SetClockTimer(int ticks) {
        clock_.clockTimer = ticks;
    }
    bool ControlsLocked() const {
        return controlsLocked_;
    }
    const PlayerStats& Stats(int player) const {
        return stats_[player];
    }

  private:
    // proplvreset2's share of loading the stage's setup file.
    bool LoadSetup(LevelId stage);

    BgSource& source_;
    BgLevel level_;
    BgVisibility visibility_;
    BgRender render_;
    PlayerCamera camera_;
    StageClock clock_;
    std::array<PlayerStats, kMaxPlayers> stats_{};
    std::optional<StageSetup> setup_;
    StageSetup::StanResolution padTiles_;
    IntroSetup intro_;
    PlayerSpawn spawn_;
    // Bond's head, created once and kept for every life.
    std::unique_ptr<HeadMotion> head_;
    PlayerMove player_;
    PlayerWeapons weapons_;
    PlayerOptions options_;
    LevelId currentStage_ = LevelId::Max;
    bool loaded_ = false;
    // D_800483C0: the level's draw switch, which the portal debug toggles.
    bool drawEnabled_ = true;
    bool controlsLocked_ = false;
    std::string error_;
};

} // namespace Frigate::Level
