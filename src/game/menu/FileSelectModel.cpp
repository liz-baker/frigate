#include "game/menu/FileSelectModel.h"

#include <cmath>

#include "game/menu/FolderScreenSpace.h"
#include "game/save/SaveData.h"
#include "platform/MenuPointer.h"

namespace Frigate::Game::Menu {

namespace {

using Input::Button::kA;
using Input::Button::kB;
using Input::Button::kCLeft;
using Input::Button::kCRight;
using Input::Button::kL;
using Input::Button::kPadLeft;
using Input::Button::kPadRight;
using Input::Button::kR;
using Input::Button::kStart;
using Input::Button::kZ;

constexpr uint16_t kSelectButtons = kA | kZ | kStart;
constexpr uint16_t kLeftButtons = kPadLeft | kL | kCLeft;
constexpr uint16_t kRightButtons = kPadRight | kR | kCRight;

// The stick positions that move the erase prompt's choice.
constexpr int kPromptStickLeft = -0x2D;
constexpr int kPromptStickRight = 0x2E;

// getPlayer_c_screenleft/top/width/height for the front-end's 440x330 view,
// and the margin the cursor keeps from its edges.
constexpr float kScreenLeft = 0.0f;
constexpr float kScreenTop = 0.0f;
constexpr float kScreenWidth = 440.0f;
constexpr float kScreenHeight = 330.0f;
constexpr float kCursorMargin = 20.0f;

// g_GlobalTimerDelta isn't advanced in the front-end and stays at the 1.0 the
// level start sets.
constexpr float kTimerDelta = 1.0f;

// toggle_deletion_menu_for_folder's offset from the folder's projected centre.
constexpr float kPromptCursorX = -1.0f;
constexpr float kPromptCursorY = 20.0f;

bool Contains(const ScreenRect& rect, const MenuCursor& cursor) {
    return rect.left <= cursor.h && cursor.h <= rect.right && rect.up <= cursor.v && cursor.v <= rect.down;
}

// The folder box is stored the other way round (see ScreenRect).
bool ContainsFolder(const ScreenRect& rect, const MenuCursor& cursor) {
    return rect.right <= cursor.h && cursor.h <= rect.left && rect.down <= cursor.v && cursor.v <= rect.up;
}

// One axis of frontUpdateControlStickPosition: the dead zone removed, and the
// result kept to +-70.
int8_t ShapeStick(int8_t stick) {
    if (stick < -5) {
        stick = static_cast<int8_t>(stick + 5);
    } else if (stick >= 6) {
        stick = static_cast<int8_t>(stick - 5);
    } else {
        stick = 0;
    }

    if (stick >= 71) {
        stick = 70;
    } else if (stick < -70) {
        stick = -70;
    }
    return stick;
}

float StickStep(int8_t stick) {
    if (stick > 0) {
        return (static_cast<float>(stick) * 0.075f + 0.5f) * kTimerDelta;
    }
    if (stick < 0) {
        return (static_cast<float>(stick) * 0.075f - 0.5f) * kTimerDelta;
    }
    return 0.0f;
}

} // namespace

void FileSelectModel::Init(FileSelectSave& save) {
    if (save.CurrentFolder() < Save::kFolder1) {
        save.SetCurrentFolder(Save::kFolder1);
    }
    folderSelectedForDeletion_ = -1;
    deletionChoice_ = 1;
}

bool FileSelectModel::ResetsIdleTimer(const Input::Sample& input) {
    return input.pressed != 0 || input.stickX < -kStickIdleLimit || input.stickX >= kStickIdleLimit + 1 ||
           input.stickY < -kStickIdleLimit || input.stickY >= kStickIdleLimit + 1;
}

void FileSelectModel::PlaceCursorOnFolder(int32_t folder, MenuCursor& cursor) {
    const FolderScreenSpace::Point2 position =
        FolderScreenSpace::ProjectToScreen(FolderScreenSpace::CameraSpacePosition(folder));
    cursor.h = position.x + kPromptCursorX;
    cursor.v = position.y + kPromptCursorY;
}

void FileSelectModel::ClampCursor(MenuCursor& cursor) {
    if ((kScreenLeft + kScreenWidth - kCursorMargin) < cursor.h) {
        cursor.h = kScreenLeft + kScreenWidth - kCursorMargin;
    } else if (cursor.h < kScreenLeft + kCursorMargin) {
        cursor.h = kScreenLeft + kCursorMargin;
    }

    if ((kScreenTop + kScreenHeight - kCursorMargin) < cursor.v) {
        cursor.v = kScreenTop + kScreenHeight - kCursorMargin;
    } else if (cursor.v < kScreenTop + kCursorMargin) {
        cursor.v = kScreenTop + kCursorMargin;
    }
}

void FileSelectModel::MoveCursorWithStick(const Input::Sample& input, MenuCursor& cursor) {
    // Every screen with a cursor comes through here; it draws its own, so the
    // system pointer is hidden (Modern keyboard and mouse only).
    Platform::Input::NoteMenuPointerUsed();
    const int8_t stickX = ShapeStick(input.stickX);
    const int8_t stickY = ShapeStick(static_cast<int8_t>(-input.stickY));

    cursor.h += StickStep(stickX);
    cursor.v += StickStep(stickY);
    ClampCursor(cursor);
}

bool FileSelectModel::MoveCursorToPointer(const Input::Sample& input, MenuCursor& cursor) {
    if (!input.pointer.active) {
        return false;
    }
    cursor.h = kScreenLeft + input.pointer.u * kScreenWidth;
    cursor.v = kScreenTop + input.pointer.v * kScreenHeight;
    ClampCursor(cursor);
    return true;
}

void FileSelectModel::UpdateErasePrompt(const Input::Sample& input, MenuCursor& cursor, FileSelectSave& save) {
    if (input.Pressed(kLeftButtons) && deletionChoice_ == 0) {
        deletionChoice_ = 1;
        RaiseSfx(FileSelectSfx::OptionClick2);
    } else if (input.Pressed(kRightButtons) && deletionChoice_ != 0) {
        deletionChoice_ = 0;
        RaiseSfx(FileSelectSfx::OptionClick2);
    }
    if (input.stickX < kPromptStickLeft && deletionChoice_ == 0) {
        deletionChoice_ = 1;
        RaiseSfx(FileSelectSfx::OptionClick2);
    } else if (input.stickX >= kPromptStickRight && deletionChoice_ != 0) {
        deletionChoice_ = 0;
        RaiseSfx(FileSelectSfx::OptionClick2);
    }

    if (input.Pressed(kSelectButtons)) {
        if (deletionChoice_ == 0) {
            save.DeleteFolder(folderSelectedForDeletion_);
        }
        RaiseSfx(FileSelectSfx::GunM60AmmGun3);
        PlaceCursorOnFolder(folderSelectedForDeletion_, cursor);
        folderSelectedForDeletion_ = -1;
    } else if (input.Pressed(kB)) {
        PlaceCursorOnFolder(folderSelectedForDeletion_, cursor);
        folderSelectedForDeletion_ = -1;
        RaiseSfx(FileSelectSfx::GunM60AmmGun3);
    }
}

void FileSelectModel::UpdateFolders(const Input::Sample& input, MenuCursor& cursor, const FileSelectLayout& layout,
                                    FileSelectSave& save) {
    for (int32_t folder = Save::kFolder1; folder < Save::kFolderCount; folder++) {
        if (!layout.folderBounds || !ContainsFolder(layout.folderBounds(folder), cursor)) {
            continue;
        }
        if (!input.Pressed(kSelectButtons)) {
            continue;
        }

        if (optionIcon_ == 0) {
            save.SetCurrentFolder(folder);
            RaiseSfx(FileSelectSfx::PaperTurn);
        } else if (optionIcon_ == 1) {
            save.CopyFolderToFirstFree(folder);
            optionIcon_ = 0;
            RaiseSfx(FileSelectSfx::CopyFile);
        } else if (optionIcon_ == 2) {
            int32_t stage = -1;
            int32_t difficulty = Save::kDifficultyMulti;
            save.HighestCompleted(folder, &stage, &difficulty);
            if (stage >= Save::kLevelDam && difficulty >= Save::kDifficultyAgent) {
                folderSelectedForDeletion_ = folder;
                deletionChoice_ = 1;
            }
            optionIcon_ = 0;
            RaiseSfx(FileSelectSfx::OptionClick2);
        }
        break;
    }

    if (input.Pressed(kSelectButtons)) {
        if (Contains(layout.copy, cursor)) {
            optionIcon_ = 1;
            RaiseSfx(FileSelectSfx::DoorLock);
        } else if (Contains(layout.erase, cursor)) {
            optionIcon_ = 2;
            RaiseSfx(FileSelectSfx::DoorLock);
        } else if (optionIcon_ != 0) {
            optionIcon_ = 0;
            RaiseSfx(FileSelectSfx::GunM60AmmGun3);
        }
    } else if (input.Pressed(kB) && optionIcon_ != 0) {
        optionIcon_ = 0;
        RaiseSfx(FileSelectSfx::GunM60AmmGun3);
    }

    MoveCursorWithStick(input, cursor);
}

FileSelectModel::Next FileSelectModel::Update(const Input::Sample& input, int timer, MenuCursor& cursor,
                                              const FileSelectLayout& layout, FileSelectSave& save) {
    sfx_.clear();

    // Coming back from the mode select with a folder still current: the cursor
    // goes onto that folder and the selection is cleared.
    int32_t current = save.CurrentFolder();
    if (current >= Save::kFolder1) {
        if (current == Save::kRamRomFolderNum) {
            current = save.PreviousFolder();
        }
        if (current < Save::kFolder1 || current >= Save::kFolderCount) {
            current = Save::kFolder1;
        }
        PlaceCursorOnFolder(current, cursor);
        save.SetCurrentFolder(-1);
    }

    if (folderSelectedForDeletion_ >= 0) {
        UpdateErasePrompt(input, cursor, save);
    } else {
        UpdateFolders(input, cursor, layout, save);
    }

    if (save.CurrentFolder() >= Save::kFolder1) {
        SetCursorForMode(cursor, 0);
        return Next::ModeSelect;
    }

    if (timer >= kIdleTimeout) {
        return Next::LegalScreen;
    }
    return Next::Stay;
}

} // namespace Frigate::Game::Menu
