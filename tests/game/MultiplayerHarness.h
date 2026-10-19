#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "AudioEngine.h"
#include "game/FrontState.h"
#include "game/LiveRun.h"
#include "game/MultiplayerSettings.h"
#include "game/Sound.h"
#include "game/menu/StageStart.h"
#include "gfx/GbiF3dex.h"
#include "platform/InputState.h"
#include "resource/type/AudioBankResource.h"
#include "resource/type/MusicSequencesResource.h"
#include "ship/resource/ResourceManager.h"

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
    // The script as it reads from `tick` on: tick 0 of the result is `tick` of
    // this one.
    PlayerScript ShiftedBy(int tick) const {
        PlayerScript shifted;
        for (const Hold_& hold : holds_) {
            shifted.holds_.push_back({ hold.from - tick, hold.to - tick, hold.stickX, hold.stickY, hold.buttons });
        }
        return shifted;
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

// The game's audio with no output: the AudioEngine from the packed audio
// archive installed as the one Sound's calls use (nothing renders a frame, so
// no sound ends by itself), and a log of every call that starts or stops a
// sound or a track. Install it before starting the session, so it is gone only
// after the stage is.
class AudioRig {
  public:
    explicit AudioRig(const std::shared_ptr<Ship::ResourceManager>& resources) {
        sfx_ = resources->LoadResource<AudioBankResource>("audio/sfx");
        instruments_ = resources->LoadResource<AudioBankResource>("audio/instruments");
        sequences_ = resources->LoadResource<MusicSequencesResource>("audio/music");
        if (sfx_ == nullptr || instruments_ == nullptr || sequences_ == nullptr) {
            return;
        }
        AudioDriver::MusicAssets music;
        music.instruments = &instruments_->Data;
        music.sequences = &sequences_->Data;
        engine_ = std::make_unique<AudioDriver::AudioEngine>(sfx_->Data, AudioDriver::EngineConfig(), &music);
        Game::Sound::Install(engine_.get());
        Game::Sound::SetCallLog(&calls_);
    }
    ~AudioRig() {
        Game::Sound::SetCallLog(nullptr);
        Game::Sound::Uninstall();
    }
    AudioRig(const AudioRig&) = delete;
    AudioRig& operator=(const AudioRig&) = delete;

    // The audio archive was there.
    bool Ok() const {
        return engine_ != nullptr;
    }
    using Call = Game::Sound::Call;
    const std::vector<Call>& Calls() const {
        return calls_;
    }
    void Clear() {
        calls_.clear();
    }
    // How many calls of `kind` there are, with `a` (the sound, flag or slot)
    // when it is not negative and likewise `b` (the track).
    int Count(Call::Kind kind, int a = -1, int b = -1) const {
        return static_cast<int>(std::count_if(calls_.begin(), calls_.end(), [&](const Call& call) {
            return call.kind == kind && (a < 0 || call.a == a) && (b < 0 || call.b == b);
        }));
    }

  private:
    std::shared_ptr<AudioBankResource> sfx_;
    std::shared_ptr<AudioBankResource> instruments_;
    std::shared_ptr<MusicSequencesResource> sequences_;
    std::unique_ptr<AudioDriver::AudioEngine> engine_;
    std::vector<Call> calls_;
};

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
        session->resources_ = resourceManager;
        return session;
    }

    // Ticks the stage `ticks` times, each player's pad reading its script (an
    // idle pad where it has none). The ports past the players are the second
    // controllers of the two-controller control styles, a player's at the
    // player's number plus the player count, and read the script of that number.
    void Run(int ticks, const Scripts& scripts = {}) {
        for (int tick = 0; tick < ticks; tick++) {
            std::array<PadSample, Platform::Input::kPortCount> samples{};
            for (int port = 0; port < Platform::Input::kPortCount; port++) {
                samples[static_cast<size_t>(port)] = scripts[static_cast<size_t>(port)].At(tick);
            }
            Platform::Input::Advance(pads_, samples);
            run_->StepPads(pads_, settings_.playerCount, Platform::Input::kNoModernInput);
        }
    }

    // Draws the stage's frame into a display list, as the game does, and
    // returns how many commands it took.
    int Render() {
        std::vector<Gfx> list(65536);
        Gfx* end = run_->Render(list.data());
        return static_cast<int>(end - list.data());
    }

    // The commands of a frame of the stage, every player's view in the order
    // the game draws them.
    std::vector<Gfx> RenderFrame() {
        std::vector<Gfx> list(262144);
        Gfx* end = run_->Render(list.data());
        list.resize(static_cast<size_t>(end - list.data()));
        return list;
    }

    // The render pass of one player's view, in which a dying player's death
    // plays out and the pad respawns them (maybe_mp_interface).
    void RenderPlayer(int player) {
        Level::Stage& stage = GetStage();
        stage.SetCurrentPlayer(player);
        renderList_.resize(65536);
        stage.Render(renderList_.data(), resources_, stage.CameraPose());
        stage.SetCurrentPlayer(0);
    }

    // Run(), with the view of every player who is dead drawn each tick, as the
    // game draws every view.
    void RunDrawingTheDead(int ticks, const Scripts& scripts = {}) {
        for (int tick = 0; tick < ticks; tick++) {
            Scripts shifted;
            for (size_t port = 0; port < shifted.size(); port++) {
                shifted[port] = scripts[port].ShiftedBy(tick);
            }
            Run(1, shifted);
            for (int player = 0; player < settings_.playerCount; player++) {
                if (GetStage().PlayerAt(player).health.IsDead()) {
                    RenderPlayer(player);
                }
            }
        }
    }

    // Run(), with every player's view drawn each tick, as the game draws them:
    // the pickups are taken in the render pass.
    void RunDrawingEveryone(int ticks, const Scripts& scripts = {}) {
        for (int tick = 0; tick < ticks; tick++) {
            Scripts shifted;
            for (size_t port = 0; port < shifted.size(); port++) {
                shifted[port] = scripts[port].ShiftedBy(tick);
            }
            Run(1, shifted);
            for (int player = 0; player < settings_.playerCount; player++) {
                RenderPlayer(player);
            }
        }
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
    // The stage has ended (bossRunTitleStage): the game goes back to the menus.
    bool Done() const {
        return run_->Done();
    }
    // array_favweapon, which a game's end stores each player's weapon of choice in.
    const Game::FavouriteWeapons& Favourites() const {
        return front_.favouriteWeapons;
    }
    // num_chars_selectable_mp, which the Extra multiplayer characters cheat raises.
    const Game::MultiplayerUnlocks& Unlocks() const {
        return front_.mpUnlocks;
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
    std::shared_ptr<Ship::ResourceManager> resources_;
    std::vector<Gfx> renderList_;
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
