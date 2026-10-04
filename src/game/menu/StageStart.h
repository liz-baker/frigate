#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "level/PlayerMove.h"

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
std::function<void()> SettingsSaver(FrontState& front);
// The same for a stage in play: the options the player has set in the watch
// (`live`, which must outlive the saver) are the ones stored.
std::function<void()> SettingsSaver(FrontState& front, const Level::PlayerOptions& live);

} // namespace Frigate::Game::Menu
