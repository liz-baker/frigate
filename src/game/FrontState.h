#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "game/FavouriteWeapons.h"
#include "game/MenuId.h"
#include "game/MultiplayerSettings.h"
#include "game/MultiplayerUnlocks.h"
#include "game/Sliders007.h"
#include "level/CheatInput.h"

namespace Frigate::Game {

namespace Save {
class SaveManager;
class SaveProgress;
struct GameSettings;
} // namespace Save

namespace Menu {
class WalletBond;
struct MissionOutcome;
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

// gamemode: what the mode select last chose; the mission select and the
// screens after it read it. GAMEMODE_INTRO until a choice is made.
enum class GameMode {
    Intro = -1,
    Solo,
    Multi,
    Cheats,
};

// The front-end globals (front.c) that more than one intro screen reads or
// writes, at their cold-boot values.
struct FrontState {
    bool isFirstTimeOnMainMenu = true;
    // is_first_time_on_legal_screen: cleared as the legal screen is left.
    bool isFirstTimeOnLegalScreen = true;
    // Set when a button press skipped an intro screen; the GoldenEye logo
    // then goes to the file select instead of the cast sequence.
    bool prevKeypresses = false;
    bool geLogoBool = false;
    bool fullActorIntro = false;
    // maybe_is_in_menu: the front-end music (the folders' track) still has to
    // start. The intro screens and a stage's return to the title set it; the
    // first screen past them clears it.
    bool maybeIsInMenu = true;
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

    // g_NewCheatUnlocked and what the last stage left for the mission end
    // screens.
    bool newCheatUnlocked = false;
    std::shared_ptr<Menu::MissionOutcome> missionOutcome;

    MenuCursor cursor;
    // The 007 options screen's sliders.
    Sliders007 sliders007;
    // tab_next_selected and tab_prev_selected: the mode, mission and difficulty
    // screens' side tabs; the file select clears them.
    bool tabNextSelected = false;
    bool tabPrevSelected = false;
    // selected_stage (a LevelId value) and briefingpage (a row of
    // mission_folder_setup_entries, -1 for BRIEFING_INVALID): the mission the
    // mission select chose, which the difficulty and briefing screens work on.
    int32_t selectedStage = -1;
    int32_t briefingPage = -1;
    // g_CheatActivated, g_AppendCheatSinglePlayer and g_AppendCheatMultiPlayer:
    // the cheats switched on for the next stage and whether the mission or
    // multiplayer screens are in their cheat variants. The file select
    // clears them.
    std::array<bool, kCheatMax> cheatActivated{};
    // cheat_available: the cheats the mode select found open in the current
    // folder, indexed by cheat id.
    std::array<bool, kCheatMax> cheatAvailable{};
    GameMode gameMode = GameMode::Intro;
    // The multiplayer game was started without the menus (the launch
    // arguments or the Dev Tools tab); it returns to the legal screen when the
    // stage ends.
    bool quickStart = false;
    bool appendCheatSinglePlayer = false;
    bool appendCheatMultiPlayer = false;
    // The first player's cheat code buffer while the title stage runs (the
    // menus); the title's load empties it.
    Level::CheatInput menuCheatInput;
    // array_favweapon and num_chars_selectable_mp: multiplayer state the run
    // keeps between games.
    FavouriteWeapons favouriteWeapons;
    MultiplayerUnlocks mpUnlocks;
    // What the multiplayer screens choose and the multiplayer game a stage
    // started in GameMode::Multi plays. The player count
    // starts at selected_num_players' 0, which the options screen replaces with
    // the connected controllers' count the first time it opens.
    MultiplayerSettings multiplayer = [] {
        MultiplayerSettings settings;
        settings.playerCount = 0;
        return settings;
    }();
    // player_has_selected_char: which players have locked in their choice on the
    // character select. The screen after it clears them, so a return to the
    // character select sees them as the last screen left them.
    std::array<bool, kMpMaxPlayers> playerHasSelectedChar{};
    // g_playerPlayerData's autoaim and sight for each player, as
    // copy_aim_settings_to_playerdata last set them.
    std::array<MpAimSetting, kMpMaxPlayers> multiplayerPlayerAim{};
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
