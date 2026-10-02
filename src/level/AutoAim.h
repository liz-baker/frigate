#pragma once

#include <functional>
#include <vector>

#include "level/Chrs.h"
#include "level/PlayerCamera.h"
#include "level/PropList.h"
#include "level/Stan.h"

namespace Frigate::Level {

// What the auto-aim search reads of the level each frame.
struct AutoAimView {
    const PropList* props = nullptr;
    Chrs* chrs = nullptr;
    Stan* stan = nullptr;
    const PlayerCamera* camera = nullptr;
    // The on-screen props, the farthest first.
    const std::vector<int>* onScreen = nullptr;
    int playerProp = -1;
    // bondviewGetPlayerDuckingHeightRelated.
    float duckingHeight = 0.0f;
    // get_bullet_angle: the crosshair on screen.
    float crosshairX = 0.0f;
    float crosshairY = 0.0f;
    // The difficulty scale (`difficulty`, from lvlSetMultipliersForDifficulty).
    float difficulty = 1.0f;
    // g_ClockTimer.
    int clockTimer = 1;
    // currentPlayerGetYAutoAimEnabled and ...XAutoAimEnabled.
    bool yEnabled = false;
    bool xEnabled = false;
    // bondviewUpdateGuardTankFlagsRelated on the player's prop: takes the player,
    // and the tank the player drives, out of the line tests (`collides` false)
    // or puts them back.
    std::function<void(bool collides)> setPlayerCollides;
};

// The target the gun swings to while the player aims
// (chrpropUpdateAutoaimTarget and the player's autoaimx, autoaimy and their
// timers).
class AutoAim {
  public:
    // The life start's values (init_player_BONDdata).
    void Reset();

    // chrpropUpdateAutoaimTarget.
    void Update(const AutoAimView& view);

    // autoaim_target_x and autoaim_target_y: whether there is a target.
    bool HasTargetX() const {
        return targetX_ >= 0;
    }
    bool HasTargetY() const {
        return targetY_ >= 0;
    }
    // autoaimx and autoaimy: where on the screen, from -1 to 1, the gun aims.
    float X() const {
        return aimX_;
    }
    float Y() const {
        return aimY_;
    }

  private:
    // chrGetOnscreenRenderBounds.
    struct Bounds {
        float aim[3] = { 0, 0, 0 };
        float xBounds[2] = { 0, 0 };
        float yBounds[2] = { 0, 0 };
    };
    bool OnscreenRenderBounds(const AutoAimView& view, int prop, Bounds* bounds) const;
    // chrpropScoreAutoAimTarget.
    float Score(const AutoAimView& view, int prop, const Bounds& bounds, float* outX, float* outY) const;
    // bondviewUpdateYAutoAimTime and ...XAutoAimTime.
    void UpdateY(int target, float aim, int clockTimer);
    void UpdateX(int target, float aim, int clockTimer);

    int targetX_ = -1;
    int targetY_ = -1;
    float aimX_ = 0.0f;
    float aimY_ = 0.0f;
    int timeX_ = -1;
    int timeY_ = -1;
};

} // namespace Frigate::Level
