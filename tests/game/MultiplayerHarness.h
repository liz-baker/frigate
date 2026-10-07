#pragma once

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "game/FrontState.h"
#include "game/LiveRun.h"
#include "game/MultiplayerSettings.h"
#include "game/menu/StageStart.h"
#include "gfx/GbiF3dex.h"
#include "platform/InputState.h"

// A headless multiplayer game driven by scripted pads. The stage starts through
// the path the multiplayer menus end in (SelectMultiplayerStage and
// LiveRun::Start), is ticked with the same TickLive the game calls each frame,
// and a case reads the stage's state between ticks. A case for a new rule
// (scores, respawns, the end of the game) is a function of (Report&,
// MultiplayerSession&) added to a case list in MultiplayerHarnessTest.cpp.
namespace Frigate::Test {

using Frigate::Platform::Input::PadSample;

class Report {
  public:
    // Records a failure naming the case, player count and `what`.
    void Check(bool condition, const char* what, ...) __attribute__((format(printf, 3, 4))) {
        checks_++;
        if (condition) {
            return;
        }
        char text[256];
        va_list args;
        va_start(args, what);
        std::vsnprintf(text, sizeof(text), what, args);
        va_end(args);
        std::fprintf(stderr, "FAIL [%s, %d players]: %s\n", caseName_.c_str(), players_, text);
        failures_++;
    }
    void Begin(const std::string& caseName, int players) {
        caseName_ = caseName;
        players_ = players;
    }
    int Failures() const {
        return failures_;
    }
    int Checks() const {
        return checks_;
    }

  private:
    std::string caseName_;
    int players_ = 0;
    int failures_ = 0;
    int checks_ = 0;
};

// What one player's pad does over a stretch of ticks, counted from the start
// of the Run() call it is passed to.
class PlayerScript {
  public:
    PlayerScript& Hold(int fromTick, int toTick, int stickX, int stickY, uint16_t buttons = 0) {
        holds_.push_back({ fromTick, toTick, static_cast<int8_t>(stickX), static_cast<int8_t>(stickY), buttons });
        return *this;
    }
    PlayerScript& Walk(int fromTick, int toTick, int forward = 70) {
        return Hold(fromTick, toTick, 0, forward);
    }
    PlayerScript& Turn(int fromTick, int toTick, int right = 70) {
        return Hold(fromTick, toTick, right, 0);
    }
    PlayerScript& Press(int fromTick, int toTick, uint16_t buttons) {
        return Hold(fromTick, toTick, 0, 0, buttons);
    }
    PadSample At(int tick) const {
        PadSample sample;
        for (const Hold_& hold : holds_) {
            if (tick >= hold.from && tick < hold.to) {
                sample.stickX = hold.stickX != 0 ? hold.stickX : sample.stickX;
                sample.stickY = hold.stickY != 0 ? hold.stickY : sample.stickY;
                sample.buttons |= hold.buttons;
            }
        }
        return sample;
    }

  private:
    struct Hold_ {
        int from;
        int to;
        int8_t stickX;
        int8_t stickY;
        uint16_t buttons;
    };
    std::vector<Hold_> holds_;
};

using Scripts = std::array<PlayerScript, Game::kMpMaxPlayers>;

using Position = std::array<float, 3>;

class MultiplayerSession {
  public:
    // Loads the stage the settings name for their players. Null, with a
    // message on stderr, when it can't be loaded.
    static std::unique_ptr<MultiplayerSession> Start(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                                     const Game::MultiplayerSettings& settings) {
        auto session = std::unique_ptr<MultiplayerSession>(new MultiplayerSession);
        session->front_.gameMode = Game::GameMode::Multi;
        if (!Game::Menu::SelectMultiplayerStage(session->front_, settings)) {
            std::fprintf(stderr, "harness: the settings name no stage\n");
            return nullptr;
        }
        session->run_ = Game::LiveRun::Start(
            resourceManager, { session->front_.selectedStage, Game::Save::kDifficultyMulti }, session->front_);
        if (!session->run_) {
            std::fprintf(stderr, "harness: the stage did not load\n");
            return nullptr;
        }
        session->settings_ = settings;
        return session;
    }

    // Ticks the stage `ticks` times, each player's pad reading its script (an
    // idle pad where it has none). Ports past the players stay idle.
    void Run(int ticks, const Scripts& scripts = {}) {
        for (int tick = 0; tick < ticks; tick++) {
            std::array<PadSample, Platform::Input::kPortCount> samples{};
            for (int player = 0; player < settings_.playerCount; player++) {
                samples[static_cast<size_t>(player)] = scripts[static_cast<size_t>(player)].At(tick);
            }
            Platform::Input::Advance(pads_, samples);
            run_->StepPads(pads_, settings_.playerCount);
        }
    }

    // Draws the stage's frame into a display list, as the game does, and
    // returns how many commands it took.
    int Render() {
        std::vector<Gfx> list(65536);
        Gfx* end = run_->Render(list.data());
        return static_cast<int>(end - list.data());
    }

    // Ticks idle until the multiplayer opening is over and the pads move the
    // players, then lets them settle for `settleTicks`. Returns the ticks it
    // waited, or -1 if the opening never ends.
    int WaitForControl(int settleTicks = 30, int limit = 900) {
        for (int waited = 0; waited < limit; waited++) {
            if (CameraMode() == Level::CameraMode::FirstPerson) {
                Run(settleTicks);
                return waited;
            }
            Run(1);
        }
        return -1;
    }
    Level::CameraMode CameraMode() {
        return GetStage().CameraPose().cameraMode;
    }

    Level::Stage& GetStage() {
        return run_->GetStage();
    }
    const Game::MultiplayerSettings& Settings() const {
        return settings_;
    }
    int Players() const {
        return settings_.playerCount;
    }
    int Ticks() {
        return GetStage().Clock().stageTicks;
    }
    // Where the player stands (collision_position).
    Position PositionOf(int player) {
        return GetStage().PlayerAt(player).move.CollisionPosition();
    }
    // Where the player faces (the horizontal look direction).
    Position LookOf(int player) {
        return GetStage().PlayerAt(player).move.LookDirection();
    }
    float HealthOf(int player) {
        return GetStage().PlayerAt(player).health.Health();
    }

  private:
    MultiplayerSession() = default;

    Game::FrontState front_;
    Game::MultiplayerSettings settings_;
    std::unique_ptr<Game::LiveRun> run_;
    Platform::Input::State pads_{};
};

// Distance between two positions on the floor plane.
inline float FloorDistance(const Position& a, const Position& b) {
    const float dx = a[0] - b[0];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dz * dz);
}

inline float Distance(const Position& a, const Position& b) {
    const float dy = a[1] - b[1];
    const float floor = FloorDistance(a, b);
    return std::sqrt(floor * floor + dy * dy);
}

} // namespace Frigate::Test
