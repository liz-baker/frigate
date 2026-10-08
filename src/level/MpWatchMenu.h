#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "level/BgView.h"
#include "level/MultiplayerScoring.h"

// The multiplayer pause and watch menu a player sees in their own view
// (mpmenu.c): which page is open, where the text of each page sits in the
// player's share of the screen, and how it is drawn. The layout is plain data
// in and out, so the pages can be checked at every player count without a
// stage or a font.
namespace Frigate::Level {

// MPMENU: the pages of a player's multiplayer watch menu.
enum class MpMenuMode : int32_t {
    GoWoc,
    Losses,
    Kills,
    Scores,
    Pause,
    Exit,
    ExitConfirm,
    Finished,
};

// A player's multiplayer watch menu (mpmenuon, mpmenumode, mpquitconfirm and
// mpjoywascentre of `struct player`) and the awards the game over shows.
struct MpMenuState {
    bool on = false;
    MpMenuMode mode = MpMenuMode::GoWoc;
    bool quitConfirm = false;
    bool joyWasCentre = false;
    // ptr_text_first_mp_award and ptr_text_second_mp_award, as indices of
    // g_AwardNames; -1 for none.
    int firstAward = -1;
    int secondAward = -1;
};

// mpwatchMenuCanGoRight and mpwatchMenuCanGoLeft: whether there is a page on
// that side of `mode`. `gameOver` is g_gameOverFlag != 0.
bool MenuCanGoRight(MpMenuMode mode, bool gameOver);
bool MenuCanGoLeft(MpMenuMode mode, bool gameOver);

// LMPMENU slots the menu's text comes from.
namespace MpMenuString {
constexpr int32_t kRank1st = 0x11;
constexpr int32_t kPlay = 0x15;
constexpr int32_t kGameOver = 0x16;
constexpr int32_t kStartToExit = 0x17;
constexpr int32_t kPaused = 0x18;
constexpr int32_t kPause = 0x19;
constexpr int32_t kExit = 0x1A;
constexpr int32_t kScores = 0x1B;
constexpr int32_t kPlayerPrefix = 0x1C;
constexpr int32_t kKills = 0x1D;
constexpr int32_t kLosses = 0x1E;
constexpr int32_t kWeaponOfChoice = 0x1F;
constexpr int32_t kCancel = 0x20;
constexpr int32_t kConfirm = 0x21;
constexpr int32_t kPressStart = 0x22;
} // namespace MpMenuString

// TEXTCOLORS: the colours of the numbers on the pages. The highlight is the
// player's own row.
enum class MpTextColour {
    GreenNormal,
    GreenHighlight,
    RedNormal,
    RedHighlight,
    BlueNormal,
    BlueHighlight,
};

// The text colours (0xRRGGBBAA) the pages draw in.
constexpr uint32_t kMpMenuGreen = 0x00FF00B0;
constexpr uint32_t kMpMenuRed = 0xFF4040B0;
constexpr uint32_t kMpMenuHighlight = 0xA0FFA0F0;
constexpr uint32_t kMpMenuHighlightOutline = 0x007000A0;
// microcode_constructor_related_to_menus' colour for the menu's dimming: black
// at 160 of 255.
constexpr uint32_t kMpMenuDim = 0x000000A0;

// One line of the menu's text: drawn from (x, y) in the colour, with the outline
// colour around it when `outlined`.
struct MpMenuText {
    std::string text;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t colour = kMpMenuGreen;
    bool outlined = false;
    uint32_t outline = 0;
};

// What the menu draws for one player: the lines in drawing order, and whether
// the view is dimmed behind them.
struct MpWatchPlan {
    // The player's menu is open: the view is dimmed and the pages' lines follow.
    bool menuOpen = false;
    // The menu is closed and the player is dead: the one line of text prompts
    // them to continue. The view's scissor is set to the whole view first.
    bool continuePrompt = false;
    std::vector<MpMenuText> texts;
};

// What a player's menu reads of the game.
struct MpWatchInputs {
    int playerCount = 2;
    int player = 0;
    // viGetViewLeft, viGetViewTop, viGetViewWidth and viGetViewHeight.
    ViewMetrics view;
    MpMenuState menu;
    // g_gameOverFlag, which is a flag and a countdown (0 while the game is on).
    int gameOverFlag = 0;
    bool stopPlay = false;
    // g_pausedFlag, and who_paused == the player.
    bool paused = false;
    bool pausedByPlayer = false;
    // chevron_glow and alt_gameover_msg.
    bool chevronGlow = false;
    bool altGameOverMessage = false;
    // What get_points_for_mp_player and write_playerrank_to_buffer read.
    ScoreBoard board;
    // bonddead, deathanimfinished and redbloodfinished.
    bool dead = false;
    bool deathAnimFinished = false;
    bool redBloodFinished = false;
    // langGet of an LMPMENU slot (MpMenuString).
    std::function<std::string_view(int32_t)> string;
    // write_playerrank_to_buffer's text.
    std::string rank;
    // frontGetPlayersFavoriteWeaponInHand(player, 0) and the two awards the game
    // over left (empty for none).
    std::string weaponOfChoice;
    std::string firstAward;
    std::string secondAward;
};

// textMeasure: the width and height of a line in the menu's font.
struct MpTextExtent {
    int32_t width = 0;
    int32_t height = 0;
};
using MpMeasure = std::function<MpTextExtent(std::string_view)>;

// mp_watch_menu_display's layout: nothing for one player. With the menu open,
// the page's title, the chevrons to the pages on either side and the page's
// numbers; with it closed, the prompt of a dead player.
MpWatchPlan PlanMpWatch(const MpWatchInputs& inputs, const MpMeasure& measure);

// display_text_for_playerdata_on_MP_menu's colour for `colour`: the text colour
// and, for a highlight, the outline.
struct MpTextStyle {
    uint32_t colour = 0;
    bool outlined = false;
    uint32_t outline = 0;
};
MpTextStyle StyleOf(MpTextColour colour);

} // namespace Frigate::Level
