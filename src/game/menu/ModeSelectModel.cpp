#include "game/menu/ModeSelectModel.h"

#include "game/menu/CheatIds.h"
#include "game/menu/FileSelectModel.h"
#include "game/menu/FolderTabs.h"

namespace Frigate::Game::Menu {

namespace {

using Input::Button::kA;
using Input::Button::kB;
using Input::Button::kStart;
using Input::Button::kZ;

constexpr uint16_t kSelectButtons = kStart | kZ | kA;

// set_cursor_to_stage_solo(0): the first mission's place on the mission
// select, where the cursor lands on the way there. The mission select owns the
// full table.
constexpr float kFirstMissionCursorH = 73.0f;
constexpr float kFirstMissionCursorV = 62.0f;

} // namespace

void ModeSelectModel::Init(FrontState& front) {
    front.gameMode = GameMode::Intro;
    front.tabNextSelected = false;
    front.tabPrevSelected = false;
}

ModeSelectModel::Next ModeSelectModel::Update(const Input::Sample& input, FrontState& front, int controllerCount,
                                              const std::function<bool(int32_t cheat)>& cheatUnlocked) {
    sfx_.clear();

    cheatMenuAvailable_ = false;
    for (int32_t cheat = Cheat::kExtraMpChars; cheat != Cheat::kInvalid; cheat++) {
        const bool unlocked = cheatUnlocked(cheat);
        front.cheatAvailable[static_cast<size_t>(cheat)] = unlocked;
        cheatMenuAvailable_ = cheatMenuAvailable_ || unlocked;
    }

    MenuCursor& cursor = front.cursor;
    tabPrevHighlight_ = false;
    highlight_ = Highlight::None;
    if (CursorOnPreviousTab(cursor)) {
        tabPrevHighlight_ = true;
        if (input.Pressed(kSelectButtons)) {
            front.tabPrevSelected = true;
            sfx_.push_back(FolderSfx::DoorMetalClose2);
        }
    } else if (cheatMenuAvailable_ && kCheatRow <= cursor.v) {
        highlight_ = Highlight::Cheats;
        if (input.Pressed(kSelectButtons)) {
            front.gameMode = GameMode::Cheats;
            sfx_.push_back(FolderSfx::DoorMetalClose);
        }
    } else if (kMultiplayerRow <= cursor.v && controllerCount >= 2) {
        highlight_ = Highlight::Multiplayer;
        if (input.Pressed(kSelectButtons)) {
            front.gameMode = GameMode::Multi;
            sfx_.push_back(FolderSfx::DoorMetalClose);
        }
    } else {
        highlight_ = Highlight::SinglePlayer;
        if (input.Pressed(kSelectButtons)) {
            front.gameMode = GameMode::Solo;
            sfx_.push_back(FolderSfx::DoorMetalClose);
        }
    }

    if (input.Pressed(kB)) {
        front.tabPrevSelected = true;
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    }
    FileSelectModel::MoveCursorWithStick(input, cursor);

    if (front.gameMode == GameMode::Solo) {
        cursor.h = kFirstMissionCursorH;
        cursor.v = kFirstMissionCursorV;
        return Next::MissionSelect;
    }
    if (front.gameMode == GameMode::Multi) {
        return Next::MultiplayerOptions;
    }
    if (front.gameMode == GameMode::Cheats) {
        return Next::Cheat;
    }
    if (front.tabPrevSelected) {
        return Next::FileSelect;
    }
    return Next::Stay;
}

} // namespace Frigate::Game::Menu
