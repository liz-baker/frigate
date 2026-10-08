#include "game/menu/StageStart.h"

#include "game/FrontState.h"
#include "game/save/GameSettings.h"
#include "game/save/SaveProgress.h"
#include "level/CheatMasks.h"

namespace Frigate::Game::Menu {

namespace {

// get_num_players() for a solo mission.
constexpr int kSoloPlayers = 1;

} // namespace

std::optional<SelectedStage> StartSelectedStage(FrontState& front) {
    if (front.selectedStage < 0 || front.progress == nullptr || front.settings == nullptr) {
        return std::nullopt;
    }
    front.progress->LoadSaveSettingsForSelectedFolder(
        [&front](const Save::SaveData& save) { Save::ApplySettings(save, kSoloPlayers, *front.settings); });
    return SelectedStage{ front.selectedStage, front.progress->SelectedDifficulty() };
}

bool SelectMultiplayerStage(FrontState& front, const MultiplayerSettings& settings) {
    const std::optional<Level::LevelId> level = settings.StageLevel();
    if (!level) {
        return false;
    }
    front.gameMode = GameMode::Multi;
    front.multiplayer = settings;
    front.selectedStage = static_cast<int32_t>(*level);
    // update_menu15_cheat's flags for the cheats the settings carry.
    for (const int32_t cheat : settings.cheats) {
        if (cheat < 1 || cheat >= static_cast<int32_t>(kCheatInvalid)) {
            return false;
        }
        front.cheatActivated[static_cast<size_t>(cheat)] = true;
        front.appendCheatSinglePlayer = true;
        if ((Level::CheatMaskField(cheat) & Level::CheatMask::kMultiplayer) != 0) {
            front.appendCheatMultiPlayer = true;
        }
    }
    return true;
}

std::function<void()> SettingsSaver(FrontState& front) {
    return [progress = front.progress]() { progress->DeleteCurrentSelectedFolder(); };
}

std::function<void()> SettingsSaver(FrontState& front, const Level::PlayerOptions& live) {
    return [&front, &live]() {
        front.settings->options = live;
        front.progress->DeleteCurrentSelectedFolder();
    };
}

} // namespace Frigate::Game::Menu
