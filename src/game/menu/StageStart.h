#pragma once

#include <cstdint>
#include <functional>
#include <optional>

namespace Frigate::Game {
struct FrontState;
}

namespace Frigate::Game::Menu {

// The stage and difficulty the front-end hands the game loop
// (bossSetLoadedStage(selected_stage) and lvlSetSelectedDifficulty(selected_difficulty)).
struct SelectedStage {
    int32_t stage;
    int32_t difficulty;
};

// init_menu0B_runstage for a stage picked through the menus, and the part of
// the stage load that follows it: fileLoadSaveSettingsForSelectedFolder sets
// the volumes and options from the selected folder's save. Null when no stage
// is selected.
std::optional<SelectedStage> StartSelectedStage(FrontState& front);

// deleteCurrentSelectedFolder as the watch menu calls it when it closes: the
// settings are saved into the selected folder if they changed.
//
// TODO(https://github.com/liz-baker/frigate/issues/247): hand this to
// Stage::SetSettingsSaver when a stage is started from the menus; it needs a
// stage the player can control.
std::function<void()> SettingsSaver(FrontState& front);

} // namespace Frigate::Game::Menu
