#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "game/MenuId.h"

namespace Frigate::Game {

namespace Save {
class SaveManager;
class SaveProgress;
struct GameSettings;
} // namespace Save

namespace Menu {
class WalletBond;
} // namespace Menu

// CHEAT_INVALID and CHEAT_MAX: the ids g_CheatActivated is indexed by.
constexpr size_t kCheatInvalid = 75;
constexpr size_t kCheatMax = 80;

// The menu cursor (cursor_h_pos and cursor_v_pos), in the front-end's
// 440x330 screen space. The folders, mode, mission and difficulty screens share
// it.
struct MenuCursor {
    float h = 220.0f;
    float v = 165.0f;
};

// The front-end globals (front.c) that more than one intro screen reads or
// writes, at their cold-boot values.
struct FrontState {
    bool isFirstTimeOnMainMenu = true;
    // Set when a button press skipped an intro screen; the GoldenEye logo
    // then goes to the file select instead of the cast sequence.
    bool prevKeypresses = false;
    bool geLogoBool = false;
    bool fullActorIntro = false;
    // intro_character_index: the cast screen's current entry.
    int introCharacterIndex = 0;
    // select_ramrom_to_play's pick: the ramrom table entry the run-stage menu
    // plays.
    std::optional<size_t> ramromIndex;
    // Play these table entries in turn instead of the random pick (a testing
    // aid; empty plays the game's own selection).
    std::vector<size_t> demoOrder;
    size_t demoOrderNext = 0;
    // The `saves` array and its file2.c routines; the file, mode and mission
    // screens read and update the player's folders through it.
    std::shared_ptr<Save::SaveManager> save;
    // The folder and difficulty selection and the progression queries over
    // `save` that the file, mode, mission and difficulty screens share.
    std::shared_ptr<Save::SaveProgress> progress;
    // The volumes and player options a folder's save stores
    // (fileSaveSettingsForFolder / fileLoadSettingsForFolder).
    std::shared_ptr<Save::GameSettings> settings;

    // walletinst: the wallet models the folder, mode and mission screens draw.
    std::shared_ptr<Menu::WalletBond> walletBond;

    MenuCursor cursor;
    // tab_next_selected and tab_prev_selected: the mode, mission and difficulty
    // screens' side tabs; the file select clears them.
    bool tabNextSelected = false;
    bool tabPrevSelected = false;
    // maybe_is_in_menu: set while the title cycle runs; the file select clears
    // it as it starts the folders' music.
    bool maybeIsInMenu = true;
    // g_CheatActivated, g_AppendCheatSinglePlayer and g_AppendCheatMultiPlayer:
    // the cheats switched on for the next stage and whether the mission or
    // multiplayer screens are in their cheat variants. The file select
    // clears them.
    std::array<bool, kCheatMax> cheatActivated{};
    bool appendCheatSinglePlayer = false;
    bool appendCheatMultiPlayer = false;
};

// setCursorPOSforMode.
inline void SetCursorForMode(MenuCursor& cursor, int mode) {
    cursor.h = 126.0f;
    cursor.v = static_cast<float>((mode * 0x20) + 0xe2);
}

// The menu the front-end returns to when the title stage loads
// (init_menus_or_reset). The frame the demo stage is requested, the run-stage
// interface still sees the playback flag set (the stage that clears it has not
// loaded yet) and asks for the legal screen, so that is where it returns
// unless a skipped screen (prev_keypresses) sends it to the file select.
inline MenuId MenuAfterTitleStageLoad(const FrontState& front) {
    return front.prevKeypresses ? MenuId::FileSelect : MenuId::LegalScreen;
}

} // namespace Frigate::Game
