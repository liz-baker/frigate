#pragma once

#include "game/menu/MissionSelectModel.h"
#include "game/save/SaveProgress.h"

namespace Frigate::Game::Menu {

// SaveProgress as the mission select's view of the current folder, shared by
// the screens that ask which difficulties a mission has open.
class SaveMissionProgress final : public MissionSelectProgress {
  public:
    SaveMissionProgress(const Save::SaveProgress& progress, bool cheatVariant)
        : progress_(progress), cheatVariant_(cheatVariant) {
    }
    bool Is007ModeUnlocked() const override {
        // get_debug_007_unlock_flag is always 0 outside the decomp's leftover
        // debug build.
        return progress_.Is007ModeUnlocked(progress_.CurrentFolder());
    }
    Save::StageStatus StatusAt(int32_t level, int32_t difficulty) const override {
        return progress_.StageStatusFor(progress_.CurrentFolder(), level, difficulty);
    }
    bool CheatVariant() const override {
        return cheatVariant_;
    }

  private:
    const Save::SaveProgress& progress_;
    bool cheatVariant_;
};

} // namespace Frigate::Game::Menu
