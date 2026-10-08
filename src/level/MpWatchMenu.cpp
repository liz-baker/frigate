#include "level/MpWatchMenu.h"

namespace Frigate::Level {

namespace {

// The rows of the US version's text, below the view's top. The PAL version
// sits every row 8 lower (MPMENU_YOFF).
constexpr int32_t kTitleRow = 22;
constexpr int32_t kRankRow = 37;
constexpr int32_t kSubtitleRow = 53;
constexpr int32_t kFirstNumberRow = 70;
constexpr int32_t kSecondNumberRow = 86;
constexpr int32_t kFirstAwardRow = 75;
constexpr int32_t kSecondAwardRow = 88;
constexpr int32_t kConfirmRow = 54;

// Columns, from the view's left. The title and the lines under it centre on
// the 160 pixel column the menu is laid out in; a view twice as wide (two
// players) is centred on it by an offset.
constexpr int32_t kCentreColumn = 80;
constexpr int32_t kTwoPlayerOffset = 80;
constexpr int32_t kLeftNumberColumn = 64;
constexpr int32_t kRightNumberColumn = 96;
constexpr int32_t kLeftChevronColumn = 40;
constexpr int32_t kRightChevronColumn = 112;
constexpr int32_t kGameOverChevronShift = 10;
constexpr int32_t kGameOverChevronNudge = 8;
constexpr int32_t kCancelColumn = 54;
constexpr int32_t kConfirmColumn = 104;
// Three or more players: the weapon of choice and awards sit this far to the
// side of the column, to opposite sides in neighbouring views.
constexpr int32_t kAwardsNudge = 7;

constexpr const char* kLeftChevron = "<";
constexpr const char* kRightChevron = ">";

struct Builder {
    const MpWatchInputs& in;
    const MpMeasure& measure;
    MpWatchPlan plan;

    void Add(std::string text, int32_t x, int32_t y, MpTextStyle style) {
        plan.texts.push_back({ std::move(text), x, y, style.colour, style.outlined, style.outline });
    }
    void AddPlain(std::string text, int32_t x, int32_t y, uint32_t colour = kMpMenuGreen) {
        Add(std::move(text), x, y, { colour, false, 0 });
    }
    void AddHighlight(std::string text, int32_t x, int32_t y) {
        Add(std::move(text), x, y, { kMpMenuHighlight, true, kMpMenuHighlightOutline });
    }
    // The text with its middle at `column`, `row` below the view's top (its
    // middle too): the centred lines of every page.
    void AddCentred(std::string text, int32_t column, int32_t row, MpTextStyle style) {
        const MpTextExtent extent = measure(text);
        const int32_t x = (in.view.viewLeft + column) - (extent.width >> 1);
        const int32_t y = (in.view.viewTop - (extent.height >> 1)) + row;
        Add(std::move(text), x, y, style);
    }
    // display_text_for_playerdata_on_MP_menu: a number centred on (x, y).
    void AddNumber(int32_t value, int32_t x, int32_t y, MpTextColour colour) {
        std::string text = std::to_string(value);
        const MpTextExtent extent = measure(text);
        Add(std::move(text), x - (extent.width >> 1), y, StyleOf(colour));
    }
};

// The column and row, below the view's top, of the number for player `slot`:
// two players share a column and sit one under the other; three and four sit in
// two by two.
int32_t NumberColumn(int playerCount, int slot) {
    if (playerCount == 2) {
        return kCentreColumn;
    }
    return (slot & 1) == 0 ? kLeftNumberColumn : kRightNumberColumn;
}

int32_t NumberRow(int playerCount, int slot) {
    if (playerCount == 2) {
        return slot == 0 ? kFirstNumberRow : kSecondNumberRow;
    }
    return slot < 2 ? kFirstNumberRow : kSecondNumberRow;
}

bool ShowsRank(const MpWatchInputs& in) {
    // mpwatchShouldDisplayRank's argument: the game is still going.
    return ShouldDisplayRank(in.board.scenario, in.gameOverFlag == 0 && !in.stopPlay);
}

bool ShowsScore(const MpWatchInputs& in) {
    return ShouldDisplayScore(in.board.scenario, in.gameOverFlag == 0 && !in.stopPlay);
}

std::string_view StringOf(const MpWatchInputs& in, int32_t slot) {
    return in.string ? in.string(slot) : std::string_view();
}

bool IsTeamScenario(Game::MpScenario scenario) {
    return scenario == Game::MpScenario::TwoVsTwo || scenario == Game::MpScenario::ThreeVsOne ||
           scenario == Game::MpScenario::TwoVsOne || scenario == Game::MpScenario::TheLivingDaylights ||
           scenario == Game::MpScenario::TheManWithTheGoldenGun;
}

} // namespace

bool MenuCanGoRight(MpMenuMode mode, bool gameOver) {
    switch (mode) {
        case MpMenuMode::GoWoc:
        case MpMenuMode::Losses:
        case MpMenuMode::Kills:
        case MpMenuMode::Pause:
            return true;
        case MpMenuMode::Scores:
            return !gameOver;
        default:
            return false;
    }
}

bool MenuCanGoLeft(MpMenuMode mode, bool gameOver) {
    switch (mode) {
        case MpMenuMode::Kills:
        case MpMenuMode::Scores:
        case MpMenuMode::Pause:
        case MpMenuMode::Exit:
            return true;
        case MpMenuMode::Losses:
            return gameOver;
        default:
            return false;
    }
}

MpTextStyle StyleOf(MpTextColour colour) {
    switch (colour) {
        case MpTextColour::GreenNormal:
            return { 0x00FF00B0, false, 0 };
        case MpTextColour::GreenHighlight:
            return { 0xA0FFA0F0, true, 0x007000A0 };
        case MpTextColour::RedNormal:
            return { 0xFF4040B0, false, 0 };
        case MpTextColour::RedHighlight:
            return { 0xFFA0A0F0, true, 0x700000A0 };
        case MpTextColour::BlueNormal:
            return { 0x4040FFB0, false, 0 };
        case MpTextColour::BlueHighlight:
            return { 0xA0A0FFF0, true, 0x000070A0 };
    }
    return {};
}

MpWatchPlan PlanMpWatch(const MpWatchInputs& in, const MpMeasure& measure) {
    Builder b{ in, measure, {} };
    if (in.playerCount == 1) {
        return b.plan;
    }

    const bool gameOver = in.gameOverFlag != 0;
    const MpMenuState& menu = in.menu;

    if (!menu.on) {
        // A dead player whose death has played out is asked to continue. There
        // is nothing to continue to in You Only Live Twice once two of the
        // player's lives are gone.
        if (in.dead && in.deathAnimFinished && in.redBloodFinished && !in.stopPlay && !gameOver) {
            int32_t killsAgainst = 0;
            for (int killer = 0; killer < in.playerCount; killer++) {
                killsAgainst += in.board.killCounts[static_cast<size_t>(killer)][static_cast<size_t>(in.player)];
            }
            if (in.board.scenario != Game::MpScenario::YouOnlyLiveTwice || killsAgainst < 2) {
                b.plan.continuePrompt = true;
                std::string text(StringOf(in, MpMenuString::kPressStart));
                const MpTextExtent extent = measure(text);
                const int32_t x = (in.view.viewLeft + (in.view.viewWidth >> 1)) - (extent.width >> 1);
                const int32_t y = (in.view.viewTop + (in.view.viewHeight >> 1)) - (extent.height >> 1);
                b.AddPlain(std::move(text), x, y);
            }
        }
        return b.plan;
    }

    b.plan.menuOpen = true;
    const int32_t column = in.playerCount == 2 ? kTwoPlayerOffset : 0;
    const int cur = in.player;
    bool selfPaused = false;

    std::string title;
    switch (menu.mode) {
        case MpMenuMode::GoWoc:
        case MpMenuMode::Losses:
        case MpMenuMode::Kills:
        case MpMenuMode::Scores:
            if (!gameOver) {
                title = StringOf(in, MpMenuString::kPlay);
            } else if (in.altGameOverMessage) {
                title = StringOf(in, MpMenuString::kGameOver);
            } else {
                title = StringOf(in, MpMenuString::kStartToExit);
            }
            break;
        case MpMenuMode::Finished:
            break;
        case MpMenuMode::Pause:
            if (in.paused) {
                title = StringOf(in, MpMenuString::kPaused);
                selfPaused = in.pausedByPlayer;
            } else {
                title = StringOf(in, MpMenuString::kPause);
            }
            break;
        case MpMenuMode::Exit:
        case MpMenuMode::ExitConfirm:
            title = StringOf(in, MpMenuString::kExit);
            break;
    }
    b.AddCentred(title, column + kCentreColumn, kTitleRow,
                 selfPaused ? MpTextStyle{ kMpMenuHighlight, true, kMpMenuHighlightOutline }
                            : MpTextStyle{ kMpMenuGreen, false, 0 });

    // The chevrons to the pages on either side blink on the game over screen
    // and sit further out there.
    const auto addChevron = [&](const char* text, int32_t x) {
        const int32_t y = in.view.viewTop + kTitleRow;
        if (in.chevronGlow) {
            b.AddHighlight(text, x, y);
        } else {
            b.AddPlain(text, x, y);
        }
    };
    if (MenuCanGoLeft(menu.mode, gameOver)) {
        int32_t x = ((in.view.viewLeft + column) - (gameOver ? kGameOverChevronShift : 0)) + kLeftChevronColumn;
        if (gameOver) {
            x -= kGameOverChevronNudge;
        }
        addChevron(kLeftChevron, x);
    }
    if (MenuCanGoRight(menu.mode, gameOver)) {
        int32_t x = ((gameOver ? kGameOverChevronShift : 0) + kRightChevronColumn) + in.view.viewLeft + column;
        if (gameOver) {
            x += kGameOverChevronNudge;
        }
        addChevron(kRightChevron, x);
    }

    const auto rank = [&] {
        if (ShowsRank(in)) {
            b.AddCentred(in.rank, column + kCentreColumn, kRankRow, { kMpMenuGreen, false, 0 });
        }
    };
    const auto numberX = [&](int slot) { return in.view.viewLeft + column + NumberColumn(in.playerCount, slot); };
    const auto numberY = [&](int slot) { return in.view.viewTop + NumberRow(in.playerCount, slot); };
    const auto kills = [&](int killer, int victim) {
        return in.board.killCounts[static_cast<size_t>(killer)][static_cast<size_t>(victim)];
    };
    const auto team = [&](int player) { return in.board.teamOrToken[static_cast<size_t>(player)]; };

    if (menu.mode == MpMenuMode::Scores || menu.mode == MpMenuMode::Pause) {
        rank();
        if (ShowsScore(in)) {
            b.AddCentred(std::string(StringOf(in, MpMenuString::kScores)), column + kCentreColumn, kSubtitleRow,
                         { kMpMenuGreen, false, 0 });

            // The team scenarios colour the numbers by team: red and blue, with
            // the player's own row highlighted.
            MpTextColour current = MpTextColour::GreenHighlight;
            MpTextColour sameTeam = MpTextColour::GreenNormal;
            MpTextColour otherTeam = MpTextColour::GreenNormal;
            if (IsTeamScenario(in.board.scenario)) {
                if (team(cur) == 0) {
                    current = MpTextColour::RedHighlight;
                    sameTeam = MpTextColour::RedNormal;
                    otherTeam = MpTextColour::BlueNormal;
                } else {
                    current = MpTextColour::BlueHighlight;
                    sameTeam = MpTextColour::BlueNormal;
                    otherTeam = MpTextColour::RedNormal;
                }
            }
            for (int slot = 0; slot < in.playerCount; slot++) {
                const MpTextColour colour =
                    slot == cur ? current : (team(slot) == team(cur) ? sameTeam : otherTeam);
                b.AddNumber(PointsFor(in.board, slot), numberX(slot), numberY(slot), colour);
            }
        }
    } else if (menu.mode == MpMenuMode::Kills || menu.mode == MpMenuMode::Losses) {
        const bool lossesPage = menu.mode == MpMenuMode::Losses;
        rank();
        // "P2 KILLS" and "P2 LOSSES".
        std::string heading(StringOf(in, MpMenuString::kPlayerPrefix));
        heading += std::to_string(cur + 1);
        heading += ' ';
        heading += StringOf(in, lossesPage ? MpMenuString::kLosses : MpMenuString::kKills);
        b.AddCentred(std::move(heading), column + kCentreColumn, kSubtitleRow,
                     { lossesPage ? kMpMenuRed : kMpMenuGreen, false, 0 });

        for (int slot = 0; slot < in.playerCount; slot++) {
            if (!lossesPage) {
                // The kills the player made of each other player.
                if (slot != cur) {
                    b.AddNumber(kills(cur, slot), numberX(slot), numberY(slot), MpTextColour::GreenNormal);
                }
            } else if (slot != cur) {
                // The times each other player killed this one.
                b.AddNumber(kills(slot, cur), numberX(slot), numberY(slot), MpTextColour::GreenNormal);
            } else if (kills(slot, slot) > 0) {
                // Their own suicides, in the player's own row.
                b.AddNumber(kills(slot, cur), numberX(slot), numberY(slot), MpTextColour::RedHighlight);
            }
        }
    } else if (menu.mode == MpMenuMode::GoWoc) {
        int32_t awardsColumn = column;
        if (in.playerCount >= 3) {
            awardsColumn = (cur & 1) != 0 ? column - kAwardsNudge : column + kAwardsNudge;
        }
        const MpTextStyle plain{ kMpMenuGreen, false, 0 };
        b.AddCentred(std::string(StringOf(in, MpMenuString::kWeaponOfChoice)), awardsColumn + kCentreColumn, kRankRow,
                     plain);
        // The Japanese version sets this line 4 lower (j_text_trigger).
        b.AddCentred(in.weaponOfChoice, awardsColumn + kCentreColumn, kSubtitleRow, plain);
        if (!in.firstAward.empty()) {
            b.AddCentred(in.firstAward, awardsColumn + kCentreColumn, kFirstAwardRow, plain);
        }
        if (!in.secondAward.empty()) {
            b.AddCentred(in.secondAward, awardsColumn + kCentreColumn, kSecondAwardRow, plain);
        }
    }

    if (menu.mode == MpMenuMode::ExitConfirm) {
        const MpTextStyle highlight{ kMpMenuHighlight, true, kMpMenuHighlightOutline };
        const MpTextStyle plain{ kMpMenuGreen, false, 0 };
        b.AddCentred(std::string(StringOf(in, MpMenuString::kCancel)), column + kCancelColumn, kConfirmRow,
                     menu.quitConfirm ? plain : highlight);
        b.AddCentred(std::string(StringOf(in, MpMenuString::kConfirm)), column + kConfirmColumn, kConfirmRow,
                     menu.quitConfirm ? highlight : plain);
    }
    return b.plan;
}

} // namespace Frigate::Level
