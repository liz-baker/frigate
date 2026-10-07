#include "game/menu/MpStageSelectModel.h"

#include "game/menu/FileSelectModel.h"
#include "game/save/SaveProgress.h"

namespace Frigate::Game::Menu {

namespace {

namespace Button = Input::Button;

constexpr uint16_t kSelectButtons = Button::kStart | Button::kZ | Button::kA;

} // namespace

void MpStageSelectModel::Init(FrontState& front) {
    front.tabNextSelected = false;
    front.tabPrevSelected = false;
    tabPrevHighlight_ = false;
    highlighted_ = static_cast<int>(MpStage::Random);
}

int MpStageSelectModel::CellAt(const MenuCursor& cursor) {
    const auto v = static_cast<int32_t>(cursor.v);
    const auto h = static_cast<int32_t>(cursor.h);

    int row = 0;
    if (v >= kThirdRowTop) {
        row = 2;
    } else if (v >= kSecondRowTop) {
        row = 1;
    }
    int column = 0;
    for (int i = kColumns - 1; i > 0; i--) {
        if (h >= kColumnLeft[i]) {
            column = i;
            break;
        }
    }
    return (row * kColumns) + column;
}

bool MpStageSelectModel::Update(const Input::Sample& input, FrontState& front) {
    sfx_.clear();
    const int players = SelectedPlayerCount(front.gameMode == GameMode::Multi, front.multiplayer.playerCount);

    if (!input.Held(kSelectButtons)) {
        tabPrevHighlight_ = false;
        if (CursorOnPreviousTab(front.cursor)) {
            tabPrevHighlight_ = true;
        } else {
            highlighted_ = MultiplayerUnlocks::HighlightFallback(*front.progress, CellAt(front.cursor), players);
        }
    }

    if (input.Pressed(kSelectButtons)) {
        if (tabPrevHighlight_) {
            front.tabPrevSelected = true;
        } else {
            front.tabNextSelected = true;
            front.multiplayer.stage = static_cast<MpStage>(highlighted_);
        }
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    } else if (input.Pressed(Button::kB)) {
        front.tabPrevSelected = true;
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    }

    FileSelectModel::MoveCursorWithStick(input, front.cursor);
    return front.tabNextSelected || front.tabPrevSelected;
}

} // namespace Frigate::Game::Menu
