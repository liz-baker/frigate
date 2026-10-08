#include "level/MpWatchMenu.h"

#include <cstdio>
#include <map>

using namespace Frigate::Level;
using Frigate::Game::MpScenario;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what, int players = 0, int player = 0) {
    if (!condition) {
        std::fprintf(stderr, "FAIL [%d players, player %d]: %s\n", players, player + 1, what);
        gFailures++;
    }
}

// Every glyph is 6 wide and a line 10 high, so a line of n characters is 6n
// wide and the layout's halving of widths is easy to follow.
MpTextExtent Measure(std::string_view text) {
    return { static_cast<int32_t>(text.size()) * 6, 10 };
}

std::string_view Strings(int32_t slot) {
    static const std::map<int32_t, std::string_view> kText = {
        { MpMenuString::kPlay, "PLAY" },
        { MpMenuString::kGameOver, "GAME OVER" },
        { MpMenuString::kStartToExit, "START TO EXIT" },
        { MpMenuString::kPaused, "PAUSED" },
        { MpMenuString::kPause, "PAUSE" },
        { MpMenuString::kExit, "EXIT" },
        { MpMenuString::kScores, "SCORES" },
        { MpMenuString::kPlayerPrefix, "P" },
        { MpMenuString::kKills, "KILLS" },
        { MpMenuString::kLosses, "LOSSES" },
        { MpMenuString::kWeaponOfChoice, "Weapon of choice:" },
        { MpMenuString::kCancel, "cancel" },
        { MpMenuString::kConfirm, "confirm" },
        { MpMenuString::kPressStart, "press start\n" },
    };
    const auto at = kText.find(slot);
    return at == kText.end() ? std::string_view() : at->second;
}

// The share of the 320 by 240 screen the player's view is.
ViewMetrics ViewOf(int players, int player) {
    ViewMetrics view;
    view.x = 320;
    view.y = 240;
    if (players == 2) {
        view.viewLeft = 0;
        view.viewTop = player == 0 ? 10 : 121;
        view.viewWidth = 320;
    } else {
        view.viewLeft = (player & 1) != 0 ? 161 : 0;
        view.viewTop = player < 2 ? 10 : 121;
        view.viewWidth = 159;
    }
    view.viewHeight = 109;
    return view;
}

MpWatchInputs Open(int players, int player, MpMenuMode mode) {
    MpWatchInputs inputs;
    inputs.playerCount = players;
    inputs.player = player;
    inputs.view = ViewOf(players, player);
    inputs.menu.on = true;
    inputs.menu.mode = mode;
    inputs.board.players = players;
    inputs.string = Strings;
    inputs.rank = "Rank: 1st";
    return inputs;
}

MpWatchPlan Plan(const MpWatchInputs& inputs) {
    return PlanMpWatch(inputs, Measure);
}

const MpMenuText* Find(const MpWatchPlan& plan, const std::string& text, int nth = 0) {
    for (const MpMenuText& line : plan.texts) {
        if (line.text == text && nth-- == 0) {
            return &line;
        }
    }
    return nullptr;
}

void TestNavigation() {
    const MpMenuMode modes[] = { MpMenuMode::GoWoc, MpMenuMode::Losses, MpMenuMode::Kills,       MpMenuMode::Scores,
                                 MpMenuMode::Pause, MpMenuMode::Exit,   MpMenuMode::ExitConfirm, MpMenuMode::Finished };
    // {right, left} while the game goes on, then at its end.
    const bool live[8][2] = { { true, false }, { true, false }, { true, true },   { true, true },
                              { true, true },  { false, true }, { false, false }, { false, false } };
    const bool over[8][2] = { { true, false }, { true, true },  { true, true },   { false, true },
                              { true, true },  { false, true }, { false, false }, { false, false } };
    for (int at = 0; at < 8; at++) {
        Check(MenuCanGoRight(modes[at], false) == live[at][0], "right while the game goes on");
        Check(MenuCanGoLeft(modes[at], false) == live[at][1], "left while the game goes on");
        Check(MenuCanGoRight(modes[at], true) == over[at][0], "right at the game over");
        Check(MenuCanGoLeft(modes[at], true) == over[at][1], "left at the game over");
    }
}

void TestStyles() {
    Check(StyleOf(MpTextColour::GreenNormal).colour == 0x00FF00B0 && !StyleOf(MpTextColour::GreenNormal).outlined,
          "green");
    Check(StyleOf(MpTextColour::GreenHighlight).colour == 0xA0FFA0F0 &&
              StyleOf(MpTextColour::GreenHighlight).outline == 0x007000A0,
          "green highlight");
    Check(StyleOf(MpTextColour::RedNormal).colour == 0xFF4040B0, "red");
    Check(StyleOf(MpTextColour::RedHighlight).colour == 0xFFA0A0F0 &&
              StyleOf(MpTextColour::RedHighlight).outline == 0x700000A0,
          "red highlight");
    Check(StyleOf(MpTextColour::BlueNormal).colour == 0x4040FFB0, "blue");
    Check(StyleOf(MpTextColour::BlueHighlight).colour == 0xA0A0FFF0 &&
              StyleOf(MpTextColour::BlueHighlight).outline == 0x000070A0,
          "blue highlight");
}

void TestClosedAndSingle() {
    MpWatchInputs single = Open(1, 0, MpMenuMode::Scores);
    Check(Plan(single).texts.empty() && !Plan(single).menuOpen, "a single player has no menu");
    single.menu.on = false;
    single.dead = single.deathAnimFinished = single.redBloodFinished = true;
    Check(Plan(single).texts.empty(), "nor a prompt");

    for (int players = 2; players <= 4; players++) {
        for (int player = 0; player < players; player++) {
            MpWatchInputs closed = Open(players, player, MpMenuMode::Scores);
            closed.menu.on = false;
            const MpWatchPlan plan = Plan(closed);
            Check(plan.texts.empty() && !plan.menuOpen && !plan.continuePrompt, "a closed menu draws nothing", players,
                  player);
        }
    }
}

// The scores page of every player at every count: the title, the rank, the
// SCORES line and one number per player in the player's own corner of the
// layout.
void TestScoresPage() {
    for (int players = 2; players <= 4; players++) {
        for (int player = 0; player < players; player++) {
            MpWatchInputs in = Open(players, player, MpMenuMode::Scores);
            for (int killer = 0; killer < players; killer++) {
                for (int victim = 0; victim < players; victim++) {
                    in.board.killCounts[static_cast<size_t>(killer)][static_cast<size_t>(victim)] =
                        killer == victim ? 0 : killer + 2 * victim;
                }
            }
            const MpWatchPlan plan = Plan(in);
            const ViewMetrics& v = in.view;
            const int32_t column = players == 2 ? 160 : 80;
            Check(plan.menuOpen && !plan.continuePrompt, "the menu is open", players, player);

            const MpMenuText* title = Find(plan, "PLAY");
            Check(title != nullptr && title->x == v.viewLeft + column - 12 && title->y == v.viewTop - 5 + 22 &&
                      title->colour == 0x00FF00B0 && !title->outlined,
                  "the title is centred a little below the view's top", players, player);
            const MpMenuText* rank = Find(plan, "Rank: 1st");
            Check(rank != nullptr && rank->x == v.viewLeft + column - 27 && rank->y == v.viewTop - 5 + 37,
                  "the rank is under it", players, player);
            const MpMenuText* scores = Find(plan, "SCORES");
            Check(scores != nullptr && scores->x == v.viewLeft + column - 18 && scores->y == v.viewTop - 5 + 53,
                  "and SCORES under that", players, player);

            // The chevrons: both sides while the game goes on.
            const MpMenuText* left = Find(plan, "<");
            const MpMenuText* right = Find(plan, ">");
            Check(left != nullptr && left->x == v.viewLeft + (players == 2 ? 80 : 0) + 40 && left->y == v.viewTop + 22,
                  "the left chevron", players, player);
            Check(right != nullptr && right->x == v.viewLeft + (players == 2 ? 80 : 0) + 112 &&
                      right->y == v.viewTop + 22,
                  "the right chevron", players, player);

            // The numbers: points are kills of the others less the player's own
            // deaths to themselves, one per player.
            int numbers = 0;
            for (const MpMenuText& line : plan.texts) {
                if (line.y >= v.viewTop + 70 && line.y <= v.viewTop + 86) {
                    numbers++;
                }
            }
            Check(numbers == players, "one number for each player", players, player);
            for (int slot = 0; slot < players; slot++) {
                const int32_t points = PointsFor(in.board, slot);
                const std::string text = std::to_string(points);
                const int32_t x = players == 2 ? v.viewLeft + 160 : v.viewLeft + ((slot & 1) != 0 ? 96 : 64);
                const int32_t y = players == 2 ? v.viewTop + (slot == 0 ? 70 : 86) : v.viewTop + (slot < 2 ? 70 : 86);
                bool found = false;
                for (const MpMenuText& line : plan.texts) {
                    const bool highlighted = line.outlined && line.colour == 0xA0FFA0F0;
                    if (line.text == text && line.y == y && line.x == x - static_cast<int32_t>(text.size()) * 3 &&
                        highlighted == (slot == player)) {
                        found = true;
                    }
                }
                Check(found, "a player's points sit in their corner, the player's own highlighted", players, player);
            }
        }
    }
}

void TestTeamColours() {
    // Flag Tag colours its numbers by team too, but never shows any.
    const MpScenario teams[] = { MpScenario::TwoVsTwo, MpScenario::ThreeVsOne, MpScenario::TwoVsOne,
                                 MpScenario::TheManWithTheGoldenGun };
    for (const MpScenario scenario : teams) {
        for (int players = 3; players <= 4; players++) {
            for (int player = 0; player < players; player++) {
                MpWatchInputs in = Open(players, player, MpMenuMode::Scores);
                in.board.scenario = scenario;
                in.board.teamOrToken = { 0, 1, 0, 1 };
                const MpWatchPlan plan = Plan(in);
                const bool red = in.board.teamOrToken[static_cast<size_t>(player)] == 0;
                for (int slot = 0; slot < players; slot++) {
                    const bool sameTeam = in.board.teamOrToken[static_cast<size_t>(slot)] ==
                                          in.board.teamOrToken[static_cast<size_t>(player)];
                    MpTextColour expected;
                    if (slot == player) {
                        expected = red ? MpTextColour::RedHighlight : MpTextColour::BlueHighlight;
                    } else if (sameTeam) {
                        expected = red ? MpTextColour::RedNormal : MpTextColour::BlueNormal;
                    } else {
                        expected = red ? MpTextColour::BlueNormal : MpTextColour::RedNormal;
                    }
                    const MpTextStyle style = StyleOf(expected);
                    const int32_t y = in.view.viewTop + (slot < 2 ? 70 : 86);
                    const int32_t x = in.view.viewLeft + ((slot & 1) != 0 ? 96 : 64);
                    bool found = false;
                    for (const MpMenuText& line : plan.texts) {
                        const std::string points = std::to_string(PointsFor(in.board, slot));
                        if (line.y == y && line.x == x - static_cast<int32_t>(points.size()) * 3 &&
                            line.colour == style.colour && line.outlined == style.outlined) {
                            found = true;
                        }
                    }
                    Check(found, "a team's numbers are red or blue, the player's own highlighted", players, player);
                }
            }
        }
    }
}

void TestScoresHiddenWhenTheScenarioHidesThem() {
    for (int players = 2; players <= 4; players++) {
        MpWatchInputs in = Open(players, 0, MpMenuMode::Scores);
        in.board.scenario = MpScenario::TheLivingDaylights;
        Check(Find(Plan(in), "SCORES") == nullptr, "Flag Tag shows no scores", players);
        Check(Find(Plan(in), "Rank: 1st") != nullptr, "but a rank", players);
        in.board.scenario = MpScenario::YouOnlyLiveTwice;
        Check(Find(Plan(in), "SCORES") == nullptr, "YOLT shows no scores", players);
        Check(Find(Plan(in), "Rank: 1st") == nullptr, "and no rank while it goes on", players);
        in.gameOverFlag = 1;
        Check(Find(Plan(in), "Rank: 1st") != nullptr, "but a rank at its end", players);
        in.gameOverFlag = 0;
        in.stopPlay = true;
        Check(Find(Plan(in), "Rank: 1st") != nullptr, "and once play has stopped", players);
    }
}

void TestKillsAndLossesPages() {
    for (int players = 2; players <= 4; players++) {
        for (int player = 0; player < players; player++) {
            for (const bool losses : { false, true }) {
                MpWatchInputs in = Open(players, player, losses ? MpMenuMode::Losses : MpMenuMode::Kills);
                for (int killer = 0; killer < players; killer++) {
                    for (int victim = 0; victim < players; victim++) {
                        in.board.killCounts[static_cast<size_t>(killer)][static_cast<size_t>(victim)] =
                            10 * (killer + 1) + victim + 1;
                    }
                }
                const MpWatchPlan plan = Plan(in);
                const ViewMetrics& v = in.view;
                const std::string heading = "P" + std::to_string(player + 1) + (losses ? " LOSSES" : " KILLS");
                const MpMenuText* line = Find(plan, heading);
                const int32_t column = players == 2 ? 160 : 80;
                Check(line != nullptr && line->x == v.viewLeft + column - static_cast<int32_t>(heading.size()) * 3 &&
                          line->y == v.viewTop - 5 + 53 && line->colour == (losses ? 0xFF4040B0u : 0x00FF00B0u),
                      "the page is headed with the player's number", players, player);
                Check(Find(plan, "Rank: 1st") != nullptr, "and a rank", players, player);

                for (int slot = 0; slot < players; slot++) {
                    const int32_t value =
                        losses ? in.board.killCounts[static_cast<size_t>(slot)][static_cast<size_t>(player)]
                               : in.board.killCounts[static_cast<size_t>(player)][static_cast<size_t>(slot)];
                    const std::string text = std::to_string(value);
                    const int32_t x = players == 2 ? v.viewLeft + 160 : v.viewLeft + ((slot & 1) != 0 ? 96 : 64);
                    const int32_t y =
                        players == 2 ? v.viewTop + (slot == 0 ? 70 : 86) : v.viewTop + (slot < 2 ? 70 : 86);
                    const MpMenuText* number = nullptr;
                    for (const MpMenuText& candidate : plan.texts) {
                        if (candidate.y == y && candidate.x == x - static_cast<int32_t>(text.size()) * 3) {
                            number = &candidate;
                        }
                    }
                    if (slot != player) {
                        Check(number != nullptr && number->text == text && number->colour == 0x00FF00B0,
                              "an opponent's count is in their corner", players, player);
                    } else if (!losses) {
                        Check(number == nullptr, "the kills page leaves the player's own corner empty", players,
                              player);
                    } else {
                        // Suicides, shown in red only if there are any.
                        Check(number != nullptr && number->colour == 0xFFA0A0F0 && number->outlined,
                              "the losses page shows the player's suicides highlighted red", players, player);
                    }
                }

                // With no suicides the losses page leaves the player's corner empty.
                if (losses) {
                    in.board.killCounts[static_cast<size_t>(player)][static_cast<size_t>(player)] = 0;
                    const MpWatchPlan quiet = Plan(in);
                    const int32_t y =
                        players == 2 ? v.viewTop + (player == 0 ? 70 : 86) : v.viewTop + (player < 2 ? 70 : 86);
                    bool corner = false;
                    for (const MpMenuText& candidate : quiet.texts) {
                        corner = corner || (candidate.y == y && candidate.outlined);
                    }
                    Check(!corner, "and nothing without any", players, player);
                }
            }
        }
    }
}

void TestPausePage() {
    for (int players = 2; players <= 4; players++) {
        MpWatchInputs in = Open(players, 1, MpMenuMode::Pause);
        const MpMenuText* pause = Find(Plan(in), "PAUSE");
        Check(pause != nullptr && !pause->outlined, "PAUSE until the game is paused", players);
        Check(Find(Plan(in), "SCORES") != nullptr, "the pause page also shows the scores", players);
        in.paused = true;
        Check(Find(Plan(in), "PAUSED") != nullptr && !Find(Plan(in), "PAUSED")->outlined,
              "PAUSED for the players who did not pause", players);
        in.pausedByPlayer = true;
        const MpMenuText* paused = Find(Plan(in), "PAUSED");
        Check(paused != nullptr && paused->outlined && paused->colour == 0xA0FFA0F0,
              "and highlighted for the player who did", players);
    }
}

void TestExitPages() {
    for (int players = 2; players <= 4; players++) {
        MpWatchInputs in = Open(players, 0, MpMenuMode::Exit);
        MpWatchPlan plan = Plan(in);
        Check(Find(plan, "EXIT") != nullptr && Find(plan, "<") != nullptr && Find(plan, ">") == nullptr,
              "the exit page has a chevron to the left only", players);
        Check(Find(plan, "cancel") == nullptr, "and asks nothing yet", players);

        in.menu.mode = MpMenuMode::ExitConfirm;
        plan = Plan(in);
        const int32_t column = players == 2 ? 80 : 0;
        const ViewMetrics& v = in.view;
        const MpMenuText* cancel = Find(plan, "cancel");
        const MpMenuText* confirm = Find(plan, "confirm");
        Check(cancel != nullptr && confirm != nullptr, "the confirm page offers both", players);
        Check(Find(plan, "<") == nullptr && Find(plan, ">") == nullptr, "and no chevrons", players);
        Check(cancel->x == v.viewLeft + column + 54 - 18 && cancel->y == v.viewTop - 5 + 54, "cancel's place", players);
        Check(confirm->x == v.viewLeft + column + 104 - 21 && confirm->y == v.viewTop - 5 + 54, "confirm's place",
              players);
        Check(cancel->outlined && !confirm->outlined, "cancel is chosen first", players);
        in.menu.quitConfirm = true;
        plan = Plan(in);
        Check(!Find(plan, "cancel")->outlined && Find(plan, "confirm")->outlined, "and confirm after", players);
    }
}

void TestGameOverPages() {
    for (int players = 2; players <= 4; players++) {
        for (int player = 0; player < players; player++) {
            MpWatchInputs in = Open(players, player, MpMenuMode::Scores);
            in.gameOverFlag = 1;
            in.altGameOverMessage = true;
            MpWatchPlan plan = Plan(in);
            const ViewMetrics& v = in.view;
            const int32_t column = players == 2 ? 80 : 0;
            Check(Find(plan, "GAME OVER") != nullptr && Find(plan, "PLAY") == nullptr, "the title is GAME OVER",
                  players, player);
            in.altGameOverMessage = false;
            Check(Find(Plan(in), "START TO EXIT") != nullptr, "and flips to START TO EXIT", players, player);
            // The chevrons sit further out, the right one absent on the last page.
            const MpMenuText* left = Find(plan, "<");
            Check(left != nullptr && left->x == v.viewLeft + column - 10 + 40 - 8, "the left chevron moves out",
                  players, player);
            Check(Find(plan, ">") == nullptr, "the scores are the last page on the right", players, player);
            in.menu.mode = MpMenuMode::Kills;
            plan = Plan(in);
            const MpMenuText* right = Find(plan, ">");
            Check(right != nullptr && right->x == v.viewLeft + column + 10 + 112 + 8, "the right chevron moves out",
                  players, player);

            // A blinking chevron is highlighted.
            in.chevronGlow = true;
            plan = Plan(in);
            Check(Find(plan, ">")->outlined && Find(plan, "<")->outlined, "the glowing chevrons are outlined", players,
                  player);

            in.menu.mode = MpMenuMode::Finished;
            plan = Plan(in);
            bool blank = false;
            for (const MpMenuText& line : plan.texts) {
                blank = blank || line.text.empty();
            }
            Check(blank && Find(plan, "<") == nullptr && Find(plan, ">") == nullptr,
                  "the finished page is blank, without chevrons", players, player);
        }
    }
}

void TestAwardsPage() {
    for (int players = 2; players <= 4; players++) {
        for (int player = 0; player < players; player++) {
            MpWatchInputs in = Open(players, player, MpMenuMode::GoWoc);
            in.gameOverFlag = 1;
            in.weaponOfChoice = "PP7";
            in.firstAward = "Most Deadly";
            in.secondAward = "Most Frantic";
            const MpWatchPlan plan = Plan(in);
            const ViewMetrics& v = in.view;
            int32_t column = players == 2 ? 80 : 0;
            if (players >= 3) {
                column += (player & 1) != 0 ? -7 : 7;
            }
            const MpMenuText* heading = Find(plan, "Weapon of choice:");
            const MpMenuText* weapon = Find(plan, "PP7");
            const MpMenuText* first = Find(plan, "Most Deadly");
            const MpMenuText* second = Find(plan, "Most Frantic");
            Check(heading && weapon && first && second, "the page shows the weapon and both awards", players, player);
            Check(heading->x == v.viewLeft + column + 80 - 51 && heading->y == v.viewTop - 5 + 37,
                  "the weapon of choice's heading", players, player);
            Check(weapon->x == v.viewLeft + column + 80 - 9 && weapon->y == v.viewTop - 5 + 53, "the weapon", players,
                  player);
            Check(first->x == v.viewLeft + column + 80 - 33 && first->y == v.viewTop - 5 + 75, "the first award",
                  players, player);
            Check(second->x == v.viewLeft + column + 80 - 36 && second->y == v.viewTop - 5 + 88, "the second award",
                  players, player);
            Check(Find(plan, "<") == nullptr && Find(plan, ">") != nullptr, "only to the right", players, player);

            in.firstAward.clear();
            in.secondAward.clear();
            Check(Plan(in).texts.size() + 2 == plan.texts.size(), "a player without awards is shown none", players,
                  player);
        }
    }
}

void TestContinuePrompt() {
    for (int players = 2; players <= 4; players++) {
        for (int player = 0; player < players; player++) {
            MpWatchInputs in = Open(players, player, MpMenuMode::Scores);
            in.menu.on = false;
            in.dead = true;
            Check(Plan(in).texts.empty(), "a player still dying has no prompt", players, player);
            in.deathAnimFinished = true;
            in.redBloodFinished = true;
            MpWatchPlan plan = Plan(in);
            const ViewMetrics& v = in.view;
            Check(plan.continuePrompt && !plan.menuOpen && plan.texts.size() == 1, "a dead player is prompted", players,
                  player);
            // The line "press start\n" measures 12 characters, 72 wide.
            Check(plan.texts[0].text == "press start\n" && plan.texts[0].x == v.viewLeft + (v.viewWidth >> 1) - 36 &&
                      plan.texts[0].y == v.viewTop + (v.viewHeight >> 1) - 5,
                  "in the middle of their view", players, player);

            in.stopPlay = true;
            Check(Plan(in).texts.empty(), "not once play has stopped", players, player);
            in.stopPlay = false;
            in.gameOverFlag = 1;
            Check(Plan(in).texts.empty(), "nor at the game over", players, player);
            in.gameOverFlag = 0;

            // You Only Live Twice has nothing to continue to after two deaths.
            in.board.scenario = MpScenario::YouOnlyLiveTwice;
            in.board.killCounts[static_cast<size_t>((player + 1) % players)][static_cast<size_t>(player)] = 1;
            Check(!Plan(in).texts.empty(), "one death in YOLT still continues", players, player);
            in.board.killCounts[static_cast<size_t>((player + 2) % players)][static_cast<size_t>(player)] += 1;
            Check(Plan(in).texts.empty(), "but two do not", players, player);
        }
    }
}

} // namespace

int main() {
    TestNavigation();
    TestStyles();
    TestClosedAndSingle();
    TestScoresPage();
    TestTeamColours();
    TestScoresHiddenWhenTheScenarioHidesThem();
    TestKillsAndLossesPages();
    TestPausePage();
    TestExitPages();
    TestGameOverPages();
    TestAwardsPage();
    TestContinuePrompt();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("all multiplayer menu layout checks passed\n");
    return 0;
}
