#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"

namespace Frigate::Game::Menu {

// rectbbox: a screen rectangle in the decomp's field names. The folders'
// boxes are projected with the sides swapped (`right` is the lower x and
// `down` the lower y, as the projection flips both), the option icons' are
// not.
struct ScreenRect {
    float left = 0.0f;
    float up = 0.0f;
    float right = 0.0f;
    float down = 0.0f;
};

// The sound effects the file select plays (sndPlaySfx), in the order they
// are raised.
enum class FileSelectSfx {
    OptionClick2,
    GunM60AmmGun3,
    PaperTurn,
    CopyFile,
    DoorLock,
};

// What the file select asks of the player's folders. SaveProgress backs it in
// the game (FileSelect.cpp).
class FileSelectSave {
  public:
    virtual ~FileSelectSave() = default;
    // selected_folder_num and selected_folder_num_copy.
    virtual int32_t CurrentFolder() const = 0;
    virtual void SetCurrentFolder(int32_t folder) = 0;
    virtual int32_t PreviousFolder() const = 0;
    // fileGetHighestStageDifficultyCompletedForFolder: -1 and -1 when nothing
    // is completed.
    virtual void HighestCompleted(int32_t folder, int32_t* stage, int32_t* difficulty) const = 0;
    // fileDeleteSaveForFolder and fileCopyFolderToFirstFree.
    virtual void DeleteFolder(int32_t folder) = 0;
    virtual void CopyFolderToFirstFree(int32_t folder) = 0;
};

// The screen areas the cursor is tested against. The folders' come from the
// projected extents of the wallet models, the icons' from where the last
// frame drew them.
struct FileSelectLayout {
    std::function<ScreenRect(int32_t folder)> folderBounds;
    ScreenRect copy;
    ScreenRect erase;
};

// The folder screen's state and logic (init_menu05_fileselect and
// interface_menu05_fileselect), apart from the drawing and the models: the
// cursor over the four folders, selecting one, the copy and erase icons and
// the erase confirmation. GoldenEye 007 has no name entry; a folder is
// identified by its position and its Bond.
class FileSelectModel {
  public:
    // 30 seconds and a frame of no input (NTSC).
    static constexpr int kIdleTimeout = 1801;
    // The stick's dead zone for resetting the idle timer.
    static constexpr int kStickIdleLimit = 5;

    enum class Next {
        Stay,
        ModeSelect,
        LegalScreen,
    };

    // init_menu05_fileselect: the current folder is at least the first, and no
    // erase is pending.
    void Init(FileSelectSave& save);

    // Whether this frame's input restarts the idle timer: a button press, or
    // more than marginal stick input. Otherwise the caller adds the frame's
    // ticks.
    static bool ResetsIdleTimer(const Input::Sample& input);

    // One frame of interface_menu05_fileselect after the timer update, with
    // `timer` the timer then. Moves `cursor`, raises sounds into Sfx() and
    // returns where the screen goes next.
    Next Update(const Input::Sample& input, int timer, MenuCursor& cursor, const FileSelectLayout& layout,
                FileSelectSave& save);

    // folder_selected_for_deletion: the folder whose erase is being confirmed,
    // or -1.
    int32_t FolderSelectedForDeletion() const {
        return folderSelectedForDeletion_;
    }
    // folder_selected_for_deletion_choice: 0 is confirm, 1 is cancel.
    int32_t DeletionChoice() const {
        return deletionChoice_;
    }
    // folder_selection_screen_option_icon: 0 none, 1 copy, 2 erase.
    int32_t OptionIcon() const {
        return optionIcon_;
    }

    // The sounds raised by the last Update.
    const std::vector<FileSelectSfx>& Sfx() const {
        return sfx_;
    }

    // toggle_deletion_menu_for_folder: the cursor onto a folder, as the
    // screen's 2D projection of its position puts it.
    static void PlaceCursorOnFolder(int32_t folder, MenuCursor& cursor);

    // frontUpdateControlStickPosition: the stick moves the cursor, kept to
    // the screen.
    static void MoveCursorWithStick(const Input::Sample& input, MenuCursor& cursor);
    // Modern-only departure (mouse pointer in the menus): puts the cursor where
    // the pointer is, through the same screen margin the stick keeps. False,
    // and the cursor untouched, when the pointer doesn't hold it.
    static bool MoveCursorToPointer(const Input::Sample& input, MenuCursor& cursor);
    // The decomp's cursor limits: the screen less a margin on each side.
    static void ClampCursor(MenuCursor& cursor);

  private:
    void RaiseSfx(FileSelectSfx sfx) {
        sfx_.push_back(sfx);
    }
    void UpdateErasePrompt(const Input::Sample& input, MenuCursor& cursor, FileSelectSave& save);
    void UpdateFolders(const Input::Sample& input, MenuCursor& cursor, const FileSelectLayout& layout,
                       FileSelectSave& save);

    int32_t folderSelectedForDeletion_ = -1;
    int32_t deletionChoice_ = 1;
    int32_t optionIcon_ = 0;
    std::vector<FileSelectSfx> sfx_;
};

} // namespace Frigate::Game::Menu
