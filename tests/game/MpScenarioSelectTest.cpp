#include "game/menu/MpScenarioSelectModel.h"

#include <cstdio>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MultiplayerOptions.h"

using namespace Frigate::Game;
using namespace Frigate::Game::Menu;
using Next = MpScenarioSelectModel::Next;
namespace Button = Frigate::Game::Input::Button;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

Input::Sample Press(uint16_t buttons) {
    return { .pressed = buttons, .held = buttons };
}

constexpr int kRowCount = 8;

struct Rig {
    FrontState front;
    MpScenarioSelectModel model;

    explicit Rig(int players) {
        front.gameMode = GameMode::Multi;
        front.multiplayer.playerCount = players;
        model.Init(front);
    }

    // Over a scenario's row.
    void CursorOnRow(int row) {
        front.cursor.h = 200.0f;
        front.cursor.v = static_cast<float>(MpScenarioSelectModel::kFirstRowTop +
                                            (row * MpScenarioSelectModel::kRowSpacing) + 2);
    }
    Next Frame(const Input::Sample& input = {}) {
        return model.Update(input, front);
    }
    // Moves onto a row and presses A on it.
    Next Choose(int row) {
        CursorOnRow(row);
        Frame();
        return Frame(Press(Button::kA));
    }
};

void TestEachPlayerCountOffersItsScenarios() {
    // mp_player_counts: five scenarios for two to four players, 2 v 2 and 3 v 1
    // for exactly four, 2 v 1 for exactly three.
    for (int players = 2; players <= 4; players++) {
        for (int scenario = 0; scenario < kRowCount; scenario++) {
            const bool expected = scenario <= 4 || (players == 4 && (scenario == 5 || scenario == 6)) ||
                                  (players == 3 && scenario == 7);
            Check(MpScenarioSelectModel::Fits(static_cast<MpScenario>(scenario), players) == expected,
                  "a scenario fits the player counts mp_player_counts gives it");
        }
    }
}

void TestTheCursorHighlightsTheRowUnderIt() {
    for (int players = 2; players <= 4; players++) {
        Rig rig(players);
        for (int row = 0; row < kRowCount; row++) {
            rig.CursorOnRow(row);
            rig.Frame();
            // A row that does not fit highlights the one above it that does.
            int expected = row;
            while (expected > 0 && !MpScenarioSelectModel::Fits(static_cast<MpScenario>(expected), players)) {
                expected--;
            }
            Check(rig.model.Highlighted() == expected + 1, "the highlight is the fitting row at or above the cursor");
        }
    }
}

void TestRowsStartOnTheirOwnLine() {
    Rig rig(4);
    rig.front.cursor = { 200.0f, static_cast<float>(MpScenarioSelectModel::kFirstRowTop + 0x16 - 1) };
    rig.Frame();
    Check(rig.model.Highlighted() == 1, "one line short of the second row is the first");
    rig.front.cursor.v += 1.0f;
    rig.Frame();
    Check(rig.model.Highlighted() == 2, "the second row starts at 0x83 plus 0x16");
    rig.front.cursor.v = 0.0f;
    rig.Frame();
    Check(rig.model.Highlighted() == 1, "above every row is the first");
}

void TestTheDefaultHighlightIsTheFirstRowAndThePreviousTabIsNone() {
    Rig rig(2);
    rig.front.cursor = { 400.0f, 300.0f };
    rig.Frame();
    Check(rig.model.TabPrevHighlighted() && rig.model.Highlighted() == 0, "the previous tab highlights no row");
    rig.front.cursor = { 200.0f, 10.0f };
    rig.Frame();
    Check(!rig.model.TabPrevHighlighted() && rig.model.Highlighted() == 1, "leaving it highlights the first row");
}

void TestAHeldButtonFreezesTheHighlight() {
    for (const uint16_t button : { Button::kA, Button::kZ }) {
        Rig rig(4);
        rig.CursorOnRow(3);
        rig.Frame();
        rig.CursorOnRow(6);
        rig.Frame({ .held = button });
        Check(rig.model.Highlighted() == 4, "the highlight stays while A or Z is held");
    }
    Rig rig(4);
    rig.CursorOnRow(3);
    rig.Frame();
    rig.CursorOnRow(6);
    rig.Frame({ .held = Button::kStart });
    Check(rig.model.Highlighted() == 7, "Start held does not freeze it");
}

void TestChoosingAPlainScenarioSetsItAndGoesBack() {
    for (int players = 2; players <= 4; players++) {
        for (int row = 0; row <= 4; row++) {
            Rig rig(players);
            const Next next = rig.Choose(row);
            Check(next == Next::Options, "a plain scenario goes back to the options");
            Check(rig.front.multiplayer.scenario == static_cast<MpScenario>(row), "and is the scenario");
            Check(rig.front.tabPrevSelected && !rig.front.tabNextSelected, "by the previous tab");
            Check(rig.model.Sfx().size() == 1 && rig.model.Sfx()[0] == FolderSfx::DoorMetalClose2, "with the door");
        }
    }
}

void TestEveryButtonThatSelectsWorks() {
    for (const uint16_t button : { Button::kA, Button::kZ, Button::kStart }) {
        Rig rig(3);
        rig.CursorOnRow(2);
        rig.Frame();
        Check(rig.Frame(Press(button)) == Next::Options, "A, Z and Start all choose");
        Check(rig.front.multiplayer.scenario == MpScenario::TheLivingDaylights, "Flag Tag is chosen");
    }
}

void TestChoosingResetsTheOptionsForTheScenario() {
    Rig rig(2);
    rig.front.multiplayer.gameLength = MpGameLength::LastAlive;
    rig.front.multiplayer.weaponSet = 0xE;
    rig.Choose(1);
    Check(rig.front.multiplayer.gameLength == MpGameLength::LastAlive, "You Only Live Twice is last alive");
    Check(rig.front.multiplayer.weaponSet == 0xD, "and caps the weapon set");

    Rig golden(2);
    golden.front.multiplayer.gameLength = MpGameLength::LastAlive;
    golden.Choose(3);
    Check(golden.front.multiplayer.weaponSet == 0xD &&
              golden.front.multiplayer.gameLength == MpGameLength::TenMinutes,
          "the Golden Gun forces its weapon and a timed game");

    Rig kill(4);
    kill.Choose(4);
    Check(kill.front.multiplayer.weaponSet == 1, "Licence to Kill forces pistols");

    Rig flag(2);
    flag.front.multiplayer.gameLength = MpGameLength::TwentyMinutes;
    flag.Choose(2);
    Check(flag.front.multiplayer.gameLength == MpGameLength::TwentyMinutes, "Flag Tag keeps a timed game");
}

void TestTeamScenariosGoToTheTeamSelect() {
    Rig four(4);
    Check(four.Choose(5) == Next::Teams && four.front.multiplayer.scenario == MpScenario::TwoVsTwo,
          "2 v 2 goes to the teams");
    Check(!four.front.tabPrevSelected, "and does not take the previous tab");

    Rig threeOne(4);
    Check(threeOne.Choose(6) == Next::Teams && threeOne.front.multiplayer.scenario == MpScenario::ThreeVsOne,
          "3 v 1 goes to the teams");

    Rig three(3);
    Check(three.Choose(7) == Next::Teams && three.front.multiplayer.scenario == MpScenario::TwoVsOne,
          "2 v 1 goes to the teams with three players");
}

void TestUnavailableScenariosCannotBeChosen() {
    // With two players the cursor over a team scenario lands on Licence to Kill.
    Rig two(2);
    for (int row = 5; row < kRowCount; row++) {
        Check(two.Choose(row) == Next::Options && two.front.multiplayer.scenario == MpScenario::LicenceToKill,
              "an unavailable row chooses the scenario above it");
    }
    Rig three(3);
    Check(three.Choose(5) == Next::Options && three.front.multiplayer.scenario == MpScenario::LicenceToKill,
          "2 v 2 is out with three players");
    Rig four(4);
    Check(four.Choose(7) == Next::Teams && four.front.multiplayer.scenario == MpScenario::ThreeVsOne,
          "2 v 1 is out with four players");
}

void TestPreviousTabAndBackLeaveWithoutChoosing() {
    Rig rig(2);
    rig.front.multiplayer.scenario = MpScenario::LicenceToKill;
    rig.front.cursor = { 400.0f, 300.0f };
    rig.Frame();
    Check(rig.Frame(Press(Button::kA)) == Next::Options, "the previous tab goes back");
    Check(rig.front.multiplayer.scenario == MpScenario::LicenceToKill, "without choosing");
    Check(rig.model.Sfx().size() == 1, "with a sound");

    Rig back(2);
    back.front.multiplayer.scenario = MpScenario::LicenceToKill;
    back.CursorOnRow(1);
    back.Frame();
    Check(back.Frame(Press(Button::kB)) == Next::Options, "B goes back");
    Check(back.front.multiplayer.scenario == MpScenario::LicenceToKill, "without choosing");
    Check(back.model.Sfx().size() == 1 && back.model.Sfx()[0] == FolderSfx::DoorMetalClose2, "with the door");
}

void TestAPressBeforeAnyHighlightChoosesNothing() {
    Rig rig(2);
    rig.front.multiplayer.scenario = MpScenario::TheLivingDaylights;
    rig.front.cursor = { 200.0f, 200.0f };
    const Next next = rig.Frame({ .pressed = Button::kStart, .held = Button::kStart | Button::kZ });
    Check(next == Next::None, "nothing is chosen while no row has been highlighted");
    Check(rig.front.multiplayer.scenario == MpScenario::TheLivingDaylights, "and the scenario is unchanged");
}

void TestTheHighlightSurvivesALeaveAndReturn() {
    Rig rig(2);
    rig.CursorOnRow(2);
    rig.Frame();
    rig.model.Init(rig.front);
    Check(rig.model.Highlighted() == 3, "the highlight is as the last visit left it");
}

void TestNothingHappensWithoutAPress() {
    Rig rig(2);
    rig.CursorOnRow(1);
    Check(rig.Frame() == Next::None && rig.model.Sfx().empty(), "an idle frame stays, silently");
}

void TestStickMovesTheCursor() {
    Rig rig(2);
    rig.front.cursor = { 220.0f, 165.0f };
    const float before = rig.front.cursor.h;
    rig.Frame({ .stickX = 60 });
    Check(rig.front.cursor.h > before, "the stick moves the cursor right");
}

} // namespace

int main() {
    TestEachPlayerCountOffersItsScenarios();
    TestTheCursorHighlightsTheRowUnderIt();
    TestRowsStartOnTheirOwnLine();
    TestTheDefaultHighlightIsTheFirstRowAndThePreviousTabIsNone();
    TestAHeldButtonFreezesTheHighlight();
    TestChoosingAPlainScenarioSetsItAndGoesBack();
    TestEveryButtonThatSelectsWorks();
    TestChoosingResetsTheOptionsForTheScenario();
    TestTeamScenariosGoToTheTeamSelect();
    TestUnavailableScenariosCannotBeChosen();
    TestPreviousTabAndBackLeaveWithoutChoosing();
    TestAPressBeforeAnyHighlightChoosesNothing();
    TestTheHighlightSurvivesALeaveAndReturn();
    TestNothingHappensWithoutAPress();
    TestStickMovesTheCursor();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    return 0;
}
