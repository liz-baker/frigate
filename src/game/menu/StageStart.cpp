#include "game/menu/StageStart.h"

#include "game/FrontState.h"
#include "game/save/GameSettings.h"
#include "game/save/SaveProgress.h"

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

std::function<void()> SettingsSaver(FrontState& front) {
    return [progress = front.progress]() { progress->DeleteCurrentSelectedFolder(); };
}

} // namespace Frigate::Game::Menu
