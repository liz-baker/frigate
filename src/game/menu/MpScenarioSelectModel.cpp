#include "game/menu/MpScenarioSelectModel.h"

#include "game/MultiplayerOptions.h"
#include "game/menu/FileSelectModel.h"

namespace Frigate::Game::Menu {

namespace {

namespace Button = Input::Button;

constexpr uint16_t kHoldButtons = Button::kZ | Button::kA;
constexpr uint16_t kSelectButtons = Button::kStart | Button::kZ | Button::kA;

constexpr int kScenarioCount = static_cast<int>(MpScenario::Count);

} // namespace

void MpScenarioSelectModel::Init(FrontState& front) {
    front.tabNextSelected = false;
    front.tabPrevSelected = false;
    tabPrevHighlight_ = false;
}

bool MpScenarioSelectModel::Fits(MpScenario scenario, int playerCount) {
    const MpPlayerRange range = MultiplayerSettings::ScenarioPlayers(scenario);
    return range.min <= playerCount && playerCount <= range.max;
}

MpScenarioSelectModel::Next MpScenarioSelectModel::Update(const Input::Sample& input, FrontState& front) {
    sfx_.clear();
    bool isTeam = false;
    MultiplayerSettings& settings = front.multiplayer;
    const int players = SelectedPlayerCount(front.gameMode == GameMode::Multi, settings.playerCount);

    if (!input.Held(kHoldButtons)) {
        tabPrevHighlight_ = false;
        if (CursorOnPreviousTab(front.cursor)) {
            tabPrevHighlight_ = true;
            highlighted_ = static_cast<int32_t>(MpScenario::Normal);
        } else {
            highlighted_ = static_cast<int32_t>(MpScenario::YouOnlyLiveTwice);
            const auto v = static_cast<int32_t>(front.cursor.v);
            for (int i = kScenarioCount - 1; i != 0; i--) {
                if (v >= kFirstRowTop + (i * kRowSpacing) && Fits(static_cast<MpScenario>(i), players)) {
                    highlighted_ = i + 1;
                    break;
                }
            }
        }
    }

    if (input.Pressed(kSelectButtons)) {
        if (tabPrevHighlight_) {
            front.tabPrevSelected = true;
        } else {
            const int scenario = highlighted_ - 1;
            // The decomp indexes its tables with a scenario it has not yet
            // highlighted; nothing is chosen then.
            if (scenario >= 0 && scenario < kScenarioCount) {
                const auto chosen = static_cast<MpScenario>(scenario);
                MpOptions::ResetForScenario(settings, chosen);
                if (chosen == MpScenario::TwoVsTwo || chosen == MpScenario::ThreeVsOne ||
                    chosen == MpScenario::TwoVsOne) {
                    isTeam = true;
                } else {
                    front.tabPrevSelected = true;
                }
            }
        }
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    } else if (input.Pressed(Button::kB)) {
        front.tabPrevSelected = true;
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    }

    FileSelectModel::MoveCursorWithStick(input, front.cursor);

    if (isTeam) {
        return Next::Teams;
    }
    return front.tabPrevSelected ? Next::Options : Next::None;
}

} // namespace Frigate::Game::Menu
