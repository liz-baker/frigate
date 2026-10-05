#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "level/DemoPlayback.h"
#include "level/PlayerCamera.h"
#include "level/ResourceBgSource.h"
#include "level/Stage.h"

union Gfx;

namespace Ship {
class ResourceManager;
}

namespace Frigate::Game {

struct DemoRunOptions {
    // ramrom_replay_handler fades to the title when the recorded random byte
    // differs from the game's. Off, a mismatch is only logged (a debugging aid
    // for following a demo past its first desync).
    bool fadeOnRandomMismatch = true;
    // Hold each frame until the recording's own clock reaches it.
    bool realtime = true;
};

// The framebuffer a stage draws into.
constexpr int32_t kStageScreenWidth = 320;
constexpr int32_t kStageScreenHeight = 240;

// lvlRender's clear of the framebuffer a stage draws into.
Gfx* ClearStageFrame(Gfx* dl);

// Where a stage puts the player when it starts: the spawn pad, looking along
// it, before the first tick. The caller sets the camera mode.
Level::PlayerPose StagePlayerStartPose(Level::Stage& stage);

// One attract-mode demo, from the stage load replay_recorded_ramrom_at_address
// asks for to the fade to the title that ends it. The random generators' state
// from before the demo comes back when it ends
// (copy_current_ingame_registers_before_ramrom_playback and
// stop_demo_playback).
class DemoRun {
  public:
    // Loads demo `index` of the ramrom table and its stage. Null when the demo
    // or its stage can't be loaded.
    static std::unique_ptr<DemoRun> Start(const std::shared_ptr<Ship::ResourceManager>& resourceManager, size_t index,
                                          const DemoRunOptions& options);
    ~DemoRun();

    // One recorded frame: ramrom_replay_handler, then the stage's tick and the
    // player's move. `buttonPressed` is joyGetButtonsPressedThisFrame(0, 0xFFFF).
    void Step(bool buttonPressed);

    // The fade to the title has finished, or an AI list or the watch ended the
    // stage: bossRunTitleStage.
    bool Done() const {
        return done_;
    }
    // A button press ended the demo, which sends the front-end to the file select.
    bool EndedByButton() const {
        return endedByButton_;
    }

    // The frame's display list.
    Gfx* Render(Gfx* dl);
    // Lines the picture is presented shifted down this frame; valid after Render().
    int32_t ScreenShakeOffset() const {
        return stage_->ScreenShakeOffset();
    }

    // Sleeps until the recording's clock reaches the end of the frame just drawn.
    void WaitForFrame() const;

  private:
    explicit DemoRun(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const DemoRunOptions& options);

    std::shared_ptr<Ship::ResourceManager> resourceManager_;
    DemoRunOptions options_;
    std::unique_ptr<Level::ResourceBgSource> source_;
    std::unique_ptr<Level::Stage> stage_;
    std::optional<Level::DemoPlayback> playback_;
    Level::PlayerPose pose_;
    uint64_t savedRandom_ = 0;
    uint64_t savedChrObjRandom_ = 0;
    int packets_ = 0;
    int seedMatches_ = 0;
    // The frames the recording has run, which paces the replay: the stage's own
    // clock stands still while the watch is open.
    int32_t demoTicks_ = 0;
    // Frames run since the recording ran out without the fade finishing.
    int framesAfterEnd_ = 0;
    bool streamEnded_ = false;
    bool done_ = false;
    bool endedByButton_ = false;
    int32_t watchState_ = 0;
    std::chrono::steady_clock::time_point start_;
};

} // namespace Frigate::Game
