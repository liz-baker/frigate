#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <utility>

#include "gfx/FixedMtx.h"
#include "gfx/Gu.h"
#include "level/Hud.h"
#include "level/PlayerMove.h"
#include "level/PlayerWeapons.h"
#include "level/WatchMenu.h"
#include "libultraship/libultra/gbi.h"
#include "model/JointDraw.h"
#include "model/ModelInstance.h"

namespace Ship {
class ResourceManager;
} // namespace Ship

namespace Frigate::Level {

class ModelFactory;

// WATCH_ANIMATION_*: the steps of raising the watch to look at it and
// lowering it again.
namespace WatchState {
constexpr int32_t Closed = 0;
// Pause initiated: any weapon in the left hand is lowered.
constexpr int32_t LowerWeapon = 1;
// The view tilts to the watch's angle.
constexpr int32_t TiltView = 2;
// The left arm is raised.
constexpr int32_t RaiseArm = 3;
// The view zooms into the watch face.
constexpr int32_t ZoomIn = 4;
// The watch is open and the game paused.
constexpr int32_t Open = 5;
// The view zooms out from the watch face.
constexpr int32_t ZoomOut = 6;
// The left arm is lowered.
constexpr int32_t LowerArm = 7;
// The view tilts back to where it was, finishing the pause.
constexpr int32_t TiltBack = 8;
constexpr int32_t Interrupted1 = 9;
constexpr int32_t Interrupted2 = 10;
constexpr int32_t Interrupted3 = 11;
// The watch menu is closing.
constexpr int32_t Closing = 12;
constexpr int32_t WaitForHands = 13;
} // namespace WatchState

// The player's timing this frame, as the watch reads it.
struct WatchTiming {
    // speedgraphframes: the frames since the last, which the watch animates
    // by even while the game is paused.
    int32_t speedFrames = 1;
    // g_ClockTimer and g_GlobalTimerDelta.
    int32_t clockTimer = 1;
    float globalTimerDelta = 1.0f;
};

// What bondviewRenderWatch reads off the player.
struct WatchView {
    const Gu::MatrixF* worldToScreen = nullptr;
    // The environment colour of the player's tile, red in the top byte.
    uint32_t tileColour = 0xFFFFFF00;
    // bondtype.
    int32_t bondType = 0;
    // What the menu behind the face draws with.
    WatchMenuFrame menu;
};

// Bond looking at his watch: the pause's state machine (bondviewWatchAnimationTick
// and trigger_solo_watch_menu), the tilt of the view and the zoom it moves
// with, and the arm and watch drawn over the level (bondviewRenderWatch).
class Watch final : public WatchHooks, public WatchMenuOwner {
  public:
    // Where the watch reaches the rest of the player. None are owned.
    void Attach(PlayerMove* player, PlayerWeapons* weapons, Hud* hud, ModelFactory* models);

    // The menu behind the watch's face: what it reads of the stage (`host`),
    // the player's options (which its pages change) and inventory.
    void AttachMenu(WatchMenuHost* host, PlayerOptions* options, const Inventory* inventory);

    // See WatchMenu::SetSettingsSaver.
    void SetSettingsSaver(std::function<void()> saver) {
        menu_.SetSettingsSaver(std::move(saver));
    }

    // The controller of this frame, which the menu navigates by.
    void SetInput(const MoveInput& input) {
        menu_.SetInput(input);
    }

    // The player was hit: an open watch redraws its gauges from the values
    // before the hit (record_damage_kills).
    void RedrawGauges() {
        if (state_ != WatchState::Closed) {
            menu_.RedrawGauges();
        }
    }

    // sub_GAME_7F0BD8F0: whether the level is drawn; the open watch hides it.
    bool LevelDrawn() const {
        return menu_.LevelDrawn();
    }

    // The watch fields of init_player_BONDdata and init_watch_at_start_of_stage.
    // `watchTime` is the time the intro says the watch shows, in ticks.
    void Reset(int32_t watchTime);

    // The frame's timing, ahead of the player's tick.
    void SetTiming(const WatchTiming& timing) {
        timing_ = timing;
    }

    // WatchHooks and WatchMenuOwner.
    int32_t State() const override {
        return state_;
    }
    void TriggerZoom(float target, float time) override;
    bool ZoomRunning() const override;
    float PageZoomScale() const override;
    float UnfoldScale() const override;
    void OnInput(bool startPressed, bool blocked) override;
    void UpdateZoom(float target) override;
    void Tick() override;

    // trigger_solo_watch_menu: opens the watch, or with `interrupt` (the
    // player dying) winds it down.
    void Trigger(bool interrupt);

    // lvlGetControlsLockedFlag's share: the watch is open or closing.
    bool ControlsLocked() const {
        return controlsLocked_;
    }
    // watch_time_0 advances a frame at a time whatever the game is doing.
    void AdvanceTime() {
        watchTime_ += timing_.speedFrames;
    }

    // g_CurrentPlayer->zoominfovy: the field of view the frame is drawn with.
    float ZoomFovY() const override {
        return zoomFovY_;
    }
    // bondviewGetPauseAnimationPercent: how far the arm is raised, 0 to 1,
    // which opens the letterbox as it goes.
    float PauseAnimationPercent() const;

    bool PausingFlag() const {
        return pausingFlag_;
    }

    // bondviewRenderWatch.
    Gfx* Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& resourceManager, const WatchView& view);
    // Starts a frame: the draws of the frame before last can go.
    void BeginFrame();

  private:
    // The pause transition, which tilts the view.
    void InitPauseTransition();
    float SetupPauseTransition(bool toPause);
    void StartPauseTransition(float duration);
    void StartUnpauseTransition(float duration);
    bool IsPauseTransitioning() const;
    void UpdatePauseTransition();
    // The arm's raise and lower.
    void SetPauseWatchRelated(float value);
    void SetPauseWatchRelatedAlt(float value);
    void StepWatchAnimation();
    // The zoom (TriggerZoom, ZoomRunning and the rest of the menu's side are
    // public).
    float ZoomTarget() const;
    void TriggerZoomTo(float fovY);
    void ZoomToWatchOnOpen();
    void ZoomFromWatchOnExit();
    void UpdateZoomIn();
    // sub_GAME_7F07E7CC: the watch model, ready to play.
    void InitModel();
    // The frame of the watch model's animation, which is how far the arm has
    // come up (the game reads it out of the model's record as
    // pause_watch_related_adjust).
    float CurrentFrame() const;

    void EnterState(int32_t state);

    PlayerMove* player_ = nullptr;
    PlayerWeapons* weapons_ = nullptr;
    Hud* hud_ = nullptr;
    ModelFactory* models_ = nullptr;
    WatchTiming timing_;

    // watch_animation_state, watch_pause_time and timer_1C4.
    int32_t state_ = WatchState::Closed;
    int32_t pauseTime_ = 0;
    int32_t timer_ = 0;
    bool pausingFlag_ = false;
    bool controlsLocked_ = false;
    // field_21C: the sound of a step has not played yet.
    bool soundPending_ = true;

    // The pause transition (pause_starting_angle .. pause_state).
    float pauseStartingAngle_ = 0.0f;
    float pauseSavedVerta_ = 0.0f;
    float pauseTargetVerta_ = 0.0f;
    float pauseTransitionTime_ = 0.0f;
    float pauseTransitionDuration_ = 0.0f;
    int32_t pauseState_ = 0;

    // step_in_view_watch_animation, pause_animation_counter,
    // pause_watch_related and pause_watch_related_scaled.
    int32_t stepInView_ = 0;
    float pauseAnimationCounter_ = 0.0f;
    float pauseWatchRelated_ = 0.0f;
    float pauseWatchRelatedScaled_ = 0.0f;

    // zoominfovy, zoomintime, zoomintimemax, zoominfovyold and zoominfovynew.
    float zoomFovY_ = 60.0f;
    float zoomTime_ = 0.0f;
    float zoomTimeMax_ = 0.0f;
    float zoomFovOld_ = 60.0f;
    float zoomFovNew_ = 60.0f;

    // watch_transition_time and watch_time_0.
    float transitionTime_ = 0.9090909f;
    int32_t watchTime_ = 0;

    // Where the watch settles in front of the view (field_1D4, field_1D8,
    // pause_watch_position) and the direction and up it faces (field_1E0 to
    // field_1F4).
    std::array<float, 3> targetPosition_ = { 0.0f, 0.0f, -25.0f };
    std::array<float, 3> targetLook_ = { 0.0f, 1.0f, 0.0f };
    std::array<float, 3> targetUp_ = { 0.0f, 0.0f, 1.0f };

    // something_with_watch_object_instance.
    std::shared_ptr<Model::ModelInstance> model_;

    // The menu behind the face.
    WatchMenu menu_;
    WatchMenuHost* host_ = nullptr;
    Mtx::FixedMtx projection_{};
    Mtx::FixedMtx faceMatrix_{};
    std::array<std::deque<Model::JointDraw>, 2> draws_;
    size_t generation_ = 0;
};

} // namespace Frigate::Level
