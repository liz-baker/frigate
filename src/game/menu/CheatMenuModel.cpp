#include "game/menu/CheatMenuModel.h"

#include "game/menu/CheatTable.h"
#include "game/menu/FileSelectModel.h"
#include "game/save/SaveData.h"

namespace Frigate::Game::Menu {

namespace {

namespace Button = Input::Button;

constexpr uint16_t kSelectButtons = Button::kA | Button::kZ;

} // namespace

void CheatMenuModel::Init(FrontState& front) {
    front.tabNextSelected = false;
    front.tabPrevSelected = false;
    previousTabHighlighted_ = false;
    totalUnlocked_ = 0;
    for (int32_t cheat = 1; cheat != static_cast<int32_t>(kCheatInvalid); cheat++) {
        if (front.cheatAvailable[static_cast<size_t>(cheat)]) {
            unlocked_[static_cast<size_t>(totalUnlocked_)] = cheat;
            totalUnlocked_++;
        }
    }
}

void CheatMenuModel::HighlightRowAtCursor(const MenuCursor& cursor) {
    highlighted_ = 0;
    if (CursorOnPreviousTab(cursor)) {
        previousTabHighlighted_ = true;
        return;
    }

    const auto cursorX = static_cast<int32_t>(cursor.h);
    const auto cursorY = static_cast<int32_t>(cursor.v);
    if (totalUnlocked_ > kColumnRows && cursorX >= kSecondColumnCursorX) {
        for (int32_t row = totalUnlocked_ - (kColumnRows + 1); row >= 0; row--) {
            if (cursorY >= (row * kRowHeight) + kRowTop) {
                highlighted_ = row + kColumnRows;
                break;
            }
        }
    }

    if (highlighted_ == 0) {
        const int32_t last = totalUnlocked_ > kColumnRows ? kColumnRows - 1 : totalUnlocked_ - 1;
        for (int32_t row = last; row >= 0; row--) {
            if (cursorY >= (row * kRowHeight) + kRowTop) {
                highlighted_ = row;
                break;
            }
        }
    }
}

CheatMenuModel::Next CheatMenuModel::Update(const Input::Sample& input, FrontState& front) {
    sfx_.clear();
    bool toggle = false;

    if (!input.Held(kSelectButtons)) {
        previousTabHighlighted_ = false;
        HighlightRowAtCursor(front.cursor);
    }

    if (input.Pressed(kSelectButtons)) {
        if (previousTabHighlighted_) {
            front.tabPrevSelected = true;
        } else {
            toggle = true;
        }
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    } else if (input.Pressed(Button::kB)) {
        front.tabPrevSelected = true;
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    }

    FileSelectModel::MoveCursorWithStick(input, front.cursor);

    if (front.tabPrevSelected) {
        SetCursorForMode(front.cursor, static_cast<int32_t>(front.gameMode));
        return Next::ModeSelect;
    }

    if (toggle) {
        const int32_t cheat = unlocked_[static_cast<size_t>(highlighted_)];
        front.cheatActivated[static_cast<size_t>(cheat)] = !front.cheatActivated[static_cast<size_t>(cheat)];
    }
    return Next::Stay;
}

void CheatMenuModel::UpdateAppendFlags(FrontState& front) {
    bool appendSinglePlayer = false;
    bool appendMultiPlayer = false;
    for (int32_t cheat = 1; cheat != static_cast<int32_t>(kCheatInvalid); cheat++) {
        if (front.cheatActivated[static_cast<size_t>(cheat)]) {
            appendSinglePlayer = true;
            if (Cheat::IsMultiplayerCheat(cheat)) {
                appendMultiPlayer = true;
            }
        }
    }
    front.appendCheatSinglePlayer = appendSinglePlayer;
    front.appendCheatMultiPlayer = appendMultiPlayer;
}

} // namespace Frigate::Game::Menu
