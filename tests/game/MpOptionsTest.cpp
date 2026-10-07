#include "game/menu/MpOptionsModel.h"

#include <cstdio>
#include <memory>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MultiplayerOptions.h"
#include "game/save/SaveProgress.h"

using namespace Frigate::Game;
using namespace Frigate::Game::Menu;
using Next = MpOptionsModel::Next;
using Row = MpOptionsModel::Row;
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

// A folder's progress and the front-end state in multiplayer mode.
struct Rig {
    std::shared_ptr<Save::MemorySaveStorage> storage = std::make_shared<Save::MemorySaveStorage>();
    std::shared_ptr<Save::SaveManager> manager;
    FrontState front;
    MpOptionsModel model;
    uint32_t draw = 0;
    std::vector<uint32_t> draws;

    Rig() {
        manager = std::make_shared<Save::SaveManager>(storage);
        manager->ValidateSaves();
        front.progress = std::make_shared<Save::SaveProgress>(manager);
        front.progress->SetCurrentFolder(0);
        front.gameMode = GameMode::Multi;
    }

    MultiplayerSettings& Settings() {
        return front.multiplayer;
    }
    void Open(int controllers) {
        model.Init(front, controllers);
    }
    // Over a row's first cursor line.
    void Cursor(int32_t v) {
        front.cursor.h = 200.0f;
        front.cursor.v = static_cast<float>(v);
    }
    std::vector<Next> Frame(const Input::Sample& input, int controllers) {
        return model.Update(input, front, controllers, [this] { return draws.at(draw++); });
    }
    // Moves the cursor onto a row, then presses A on it.
    std::vector<Next> Choose(int32_t v, int controllers) {
        Cursor(v);
        Frame({}, controllers);
        return Frame(Press(Button::kA), controllers);
    }
};

bool Is(const std::vector<Next>& next, Next expected) {
    return next.size() == 1 && next[0] == expected;
}

void TestPlayerCountFollowsTheControllers() {
    for (int controllers = 2; controllers <= 4; controllers++) {
        Rig rig;
        rig.Open(controllers);
        Check(rig.Settings().playerCount == controllers, "the first visit takes every connected controller");
    }
    Rig rig;
    rig.Open(1);
    Check(rig.Settings().playerCount == 2, "never fewer than two");
}

void TestLaterVisitsKeepTheCount() {
    Rig rig;
    rig.Open(4);
    MpOptions::InitForPlayerCount(rig.Settings(), 3);
    rig.Open(4);
    Check(rig.Settings().playerCount == 3, "a second visit keeps what was chosen");
}

void TestUnplugPullsTheCountDown() {
    Rig rig;
    rig.Open(4);
    rig.Frame({}, 3);
    Check(rig.Settings().playerCount == 3, "a controller unplugged lowers four to three");
    rig.Frame({}, 2);
    Check(rig.Settings().playerCount == 2, "and again to two");
    Check(rig.Frame({}, 4).empty() && rig.Settings().playerCount == 2, "plugging in does not raise it");
}

void TestTooFewControllersLeave() {
    Rig rig;
    rig.Open(2);
    const auto next = rig.Frame({}, 1);
    Check(Is(next, Next::ModeSelect), "one controller goes back to the mode select");
    Check(rig.front.cursor.h == 126.0f && rig.front.cursor.v == static_cast<float>(1 * 0x20 + 0xE2),
          "with the cursor on the multiplayer entry");
    Check(rig.Settings().playerCount == 2, "and two players");
}

void TestPlayersRowCycles() {
    Rig rig;
    rig.Open(4);
    rig.Settings().playerCount = 2;
    const int32_t row = 0x78;
    Check(rig.Choose(row, 4).empty() && rig.Settings().playerCount == 3, "two goes to three");
    rig.Frame({}, 4);
    rig.Choose(row, 4);
    Check(rig.Settings().playerCount == 4, "three goes to four");
    rig.Frame({}, 4);
    rig.Choose(row, 4);
    Check(rig.Settings().playerCount == 2, "four wraps to two");
    Check(rig.model.Sfx().size() == 1 && rig.model.Sfx()[0] == FolderSfx::DoorMetalClose2, "with the door sound");

    Rig three;
    three.Open(3);
    three.Settings().playerCount = 2;
    three.Choose(row, 3);
    Check(three.Settings().playerCount == 3, "three controllers reach three");
    three.Frame({}, 3);
    three.Choose(row, 3);
    Check(three.Settings().playerCount == 2, "and then wrap to two");

    Rig two;
    two.Open(2);
    two.Choose(row, 2);
    Check(two.Settings().playerCount == 2, "two controllers stay at two");
}

void TestScenarioLimitsFollowThePlayerCount() {
    MultiplayerSettings s;
    MpOptions::ResetForScenario(s, MpScenario::TwoVsTwo);
    MpOptions::InitForPlayerCount(s, 4);
    Check(s.scenario == MpScenario::TwoVsTwo, "2 vs 2 holds at four players");
    MpOptions::InitForPlayerCount(s, 3);
    Check(s.scenario == MpScenario::Normal, "2 vs 2 does not fit three");

    MpOptions::ResetForScenario(s, MpScenario::ThreeVsOne);
    MpOptions::InitForPlayerCount(s, 2);
    Check(s.scenario == MpScenario::Normal, "3 vs 1 does not fit two");
    MpOptions::ResetForScenario(s, MpScenario::ThreeVsOne);
    MpOptions::InitForPlayerCount(s, 4);
    Check(s.scenario == MpScenario::ThreeVsOne, "3 vs 1 holds at four");

    MpOptions::ResetForScenario(s, MpScenario::TwoVsOne);
    MpOptions::InitForPlayerCount(s, 3);
    Check(s.scenario == MpScenario::TwoVsOne, "2 vs 1 holds at three");
    MpOptions::InitForPlayerCount(s, 4);
    Check(s.scenario == MpScenario::Normal, "2 vs 1 does not fit four");
    MpOptions::ResetForScenario(s, MpScenario::TwoVsOne);
    MpOptions::InitForPlayerCount(s, 2);
    Check(s.scenario == MpScenario::Normal, "2 vs 1 does not fit two");

    for (const MpScenario scenario : { MpScenario::YouOnlyLiveTwice, MpScenario::TheLivingDaylights,
                                       MpScenario::TheManWithTheGoldenGun, MpScenario::LicenceToKill }) {
        for (int players = 2; players <= 4; players++) {
            MpOptions::ResetForScenario(s, scenario);
            MpOptions::InitForPlayerCount(s, players);
            Check(s.scenario == scenario, "the free-for-all scenarios fit two, three and four");
        }
    }
}

void TestStageFallsBackToFit() {
    MultiplayerSettings s;
    s.stage = MpStage::Egypt;
    MpOptions::InitForPlayerCount(s, 2);
    Check(s.stage == MpStage::Egypt, "two fit Egypt");
    MpOptions::InitForPlayerCount(s, 3);
    Check(s.stage == MpStage::Temple, "three do not");
    s.stage = MpStage::Caverns;
    MpOptions::InitForPlayerCount(s, 3);
    Check(s.stage == MpStage::Caverns, "three fit the Caverns");
    MpOptions::InitForPlayerCount(s, 4);
    Check(s.stage == MpStage::Temple, "four do not");
}

void TestNewPlayersGetTheirOwnCharacter() {
    MultiplayerSettings s;
    s.character = { -1, -1, -1, -1 };
    s.handicap = { 2, 2, 2, 2 };
    MpOptions::InitForPlayerCount(s, 3);
    Check(s.character[0] == 0 && s.character[1] == 1 && s.character[2] == 2, "each player gets their row");
    Check(s.character[3] == -1, "a fourth player not in the game keeps none");
    Check(s.handicap[0] == 5 && s.handicap[2] == 5 && s.handicap[3] == 2, "and the normal handicap");
    s.character[1] = 9;
    s.handicap[1] = 1;
    MpOptions::InitForPlayerCount(s, 4);
    Check(s.character[1] == 9 && s.handicap[1] == 1, "a chosen character and handicap stay");
    Check(s.character[3] == 3 && s.handicap[3] == 5, "and the fourth joins");
}

void TestControlStylesFitThePlayerCount() {
    using Level::ControlConfig;
    MultiplayerSettings s;
    s.controlStyle = { ControlConfig::Plenty, ControlConfig::Goodhead, ControlConfig::Kissy, ControlConfig::Domino };
    MpOptions::InitForPlayerCount(s, 2);
    Check(s.controlStyle[0] == ControlConfig::Plenty && s.controlStyle[1] == ControlConfig::Goodhead,
          "two players keep the two-controller styles");
    MpOptions::InitForPlayerCount(s, 3);
    Check(s.controlStyle[0] == ControlConfig::Honey && s.controlStyle[1] == ControlConfig::Honey &&
              s.controlStyle[2] == ControlConfig::Kissy,
          "three players are put on one-controller styles");
    Check(s.controlStyle[3] == ControlConfig::Domino, "a player not in the game is left alone");
    MpOptions::InitForPlayerCount(s, 4);
    Check(s.controlStyle[3] == ControlConfig::Honey, "four players cover the fourth");
}

void TestScenarioDefaults() {
    MultiplayerSettings s;
    s.gameLength = MpGameLength::LastAlive;
    s.weaponSet = 0xE;
    MpOptions::ResetForScenario(s, MpScenario::Normal);
    Check(s.gameLength == MpGameLength::TenMinutes && s.weaponSet == 0xD, "Normal drops Last Alive and caps the weapons");

    MpOptions::ResetForScenario(s, MpScenario::YouOnlyLiveTwice);
    Check(s.gameLength == MpGameLength::LastAlive, "You Only Live Twice is last alive");
    Check(!MpOptions::UnlocksFor(s.scenario).gameLength && MpOptions::UnlocksFor(s.scenario).weaponSelect,
          "and locks only the game length");

    s.gameLength = MpGameLength::TenPoints;
    MpOptions::ResetForScenario(s, MpScenario::TheLivingDaylights);
    Check(s.gameLength == MpGameLength::TenMinutes, "Flag Tag has only timed games");
    s.gameLength = MpGameLength::FiveMinutes;
    MpOptions::ResetForScenario(s, MpScenario::TheLivingDaylights);
    Check(s.gameLength == MpGameLength::FiveMinutes, "and keeps a timed one");

    s.weaponSet = 4;
    MpOptions::ResetForScenario(s, MpScenario::TheManWithTheGoldenGun);
    Check(s.weaponSet == 0xD && !MpOptions::UnlocksFor(s.scenario).weaponSelect, "the Golden Gun forces its weapon");
    MpOptions::ResetForScenario(s, MpScenario::LicenceToKill);
    Check(s.weaponSet == 1 && !MpOptions::UnlocksFor(s.scenario).handicap, "Licence to Kill forces pistols");

    for (const MpScenario scenario : { MpScenario::Normal, MpScenario::TwoVsTwo, MpScenario::ThreeVsOne,
                                       MpScenario::TwoVsOne, MpScenario::TheLivingDaylights }) {
        const MpOptions::RowUnlocks unlocks = MpOptions::UnlocksFor(scenario);
        Check(unlocks.stageSelect && unlocks.gameLength && unlocks.chars && unlocks.weaponSelect && unlocks.handicap &&
                  unlocks.controlStyle && unlocks.aimSight,
              "every other scenario leaves every row open");
    }
}

void TestGameLengthCycles() {
    MultiplayerSettings s;
    s.gameLength = MpGameLength::Unlimited;
    std::vector<MpGameLength> seen;
    for (int i = 0; i < 7; i++) {
        MpOptions::SelectGameLength(s);
        seen.push_back(s.gameLength);
    }
    Check(seen[0] == MpGameLength::FiveMinutes && seen[5] == MpGameLength::TwentyPoints &&
              seen[6] == MpGameLength::Unlimited,
          "seven lengths, never last alive");

    MpOptions::ResetForScenario(s, MpScenario::TheLivingDaylights);
    s.gameLength = MpGameLength::Unlimited;
    for (int i = 0; i < 3; i++) {
        MpOptions::SelectGameLength(s);
    }
    Check(s.gameLength == MpGameLength::TwentyMinutes, "Flag Tag reaches twenty minutes");
    MpOptions::SelectGameLength(s);
    Check(s.gameLength == MpGameLength::Unlimited, "and wraps to unlimited");
}

void TestWeaponSetCycles() {
    MultiplayerSettings s;
    s.weaponSet = 0;
    for (int i = 0; i < 13; i++) {
        MpOptions::IncrementWeaponSet(s);
    }
    Check(s.weaponSet == 13, "thirteen steps reach the golden gun");
    MpOptions::IncrementWeaponSet(s);
    Check(s.weaponSet == 0, "which wraps to slappers only");
    s.weaponSet = 0xE;
    MpOptions::IncrementWeaponSet(s);
    Check(s.weaponSet == 0xF % 0xE, "any value past the table wraps modulo fourteen");

    Check(MpOptions::WeaponSetTextSlot(0) == 0 && MpOptions::WeaponSetTextSlot(1) == 1, "slappers and pistols");
    Check(MpOptions::WeaponSetTextSlot(2) == 0xD, "the knives are the third set");
    Check(MpOptions::WeaponSetTextSlot(3) == 2 && MpOptions::WeaponSetTextSlot(13) == 0xC, "the rest follow");
}

void TestAimCycleAndCopy() {
    MultiplayerSettings s;
    Check(s.aimSetting == 3, "sight and autoaim are on at first");
    MpOptions::AdvanceAimSetting(s);
    Check(s.aimSetting == 0, "and wrap to both off");
    MpOptions::AdvanceAimSetting(s);
    const auto aim = MpOptions::CopyAimSettingsToPlayerData(s);
    for (int player = 0; player < kMpMaxPlayers; player++) {
        Check(aim[static_cast<size_t>(player)].sight == 1 && aim[static_cast<size_t>(player)].autoaim == 0,
              "every player gets sight on and autoaim off");
    }
}

void OpenEveryStage(Rig& rig) {
    for (int32_t level = 0; level <= 20; level++) {
        rig.front.progress->UnlockStage(0, level, Save::kDifficultyAgent, 100);
    }
}

void TestFirstOpenReplacesALockedStage() {
    Rig rig;
    rig.Settings().playerCount = 3;
    rig.Settings().stage = MpStage::Bunker;
    rig.Open(3);
    Check(rig.Settings().stage == MpStage::Temple, "a stage the folder has not unlocked becomes Temple");

    Rig unlocked;
    OpenEveryStage(unlocked);
    unlocked.Settings().playerCount = 3;
    unlocked.Settings().stage = MpStage::Bunker;
    unlocked.Open(3);
    Check(unlocked.Settings().stage == MpStage::Bunker, "an unlocked one is kept");

    Rig four;
    four.Settings().playerCount = 4;
    four.Settings().stage = MpStage::Caves;
    four.Open(4);
    Check(four.Settings().stage == MpStage::Caves, "an open stage is kept");
}

void TestInitClearsTheTabs() {
    Rig rig;
    rig.front.tabPrevSelected = true;
    rig.front.tabNextSelected = true;
    rig.Open(2);
    Check(!rig.front.tabPrevSelected && !rig.front.tabNextSelected, "opening clears both tabs");
    Check(!rig.model.HighlightedRow() && !rig.model.TabPrevHighlighted() && !rig.model.TabStartHighlighted(),
          "and every highlight");
}

void TestHighlightFollowsTheCursor() {
    Rig rig;
    rig.Open(4);
    struct Case {
        int32_t v;
        Row row;
    };
    const Case cases[] = {
        { 0x00, Row::Players },       { 0x8C, Row::Players },  { 0x8D, Row::Scenario },
        { 0xA0, Row::Scenario },      { 0xA1, Row::Stage },    { 0xB5, Row::GameLength },
        { 0xC9, Row::Weapons },       { 0xDD, Row::Character }, { 0xF1, Row::Health },
        { 0x105, Row::ControlStyle }, { 0x119, Row::Aim },     { 0x140, Row::Aim },
    };
    for (const Case& c : cases) {
        rig.Cursor(c.v);
        rig.Frame({}, 4);
        Check(rig.model.HighlightedRow() == c.row, "the cursor's line picks the row");
    }

    rig.front.cursor.h = 400.0f;
    rig.front.cursor.v = 100.0f;
    rig.Frame({}, 4);
    Check(rig.model.TabStartHighlighted() && !rig.model.HighlightedRow(), "the start tab takes the cursor");
    rig.front.cursor.v = 300.0f;
    rig.Frame({}, 4);
    Check(rig.model.TabPrevHighlighted() && !rig.model.TabStartHighlighted(), "so does the previous tab");
}

void TestLockedRowsFallThrough() {
    Rig rig;
    rig.Open(4);
    MpOptions::ResetForScenario(rig.Settings(), MpScenario::YouOnlyLiveTwice);
    rig.Cursor(0xB5);
    rig.Frame({}, 4);
    Check(rig.model.HighlightedRow() == Row::Stage, "the locked game length is skipped");
    const auto length = rig.Settings().gameLength;
    Check(Is(rig.Frame(Press(Button::kA), 4), Next::StageSelect) && rig.Settings().gameLength == length,
          "and pressing it opens the stage select instead");

    MpOptions::ResetForScenario(rig.Settings(), MpScenario::TheManWithTheGoldenGun);
    rig.Cursor(0xC9);
    rig.Frame({}, 4);
    Check(rig.model.HighlightedRow() == Row::GameLength, "the locked weapons are skipped");

    MpOptions::ResetForScenario(rig.Settings(), MpScenario::LicenceToKill);
    rig.Cursor(0xF1);
    rig.Frame({}, 4);
    Check(rig.model.HighlightedRow() == Row::Character, "the locked handicap is skipped");
}

void TestHeldButtonKeepsTheHighlight() {
    Rig rig;
    rig.Open(4);
    rig.Cursor(0xB5);
    rig.Frame({}, 4);
    rig.Cursor(0x8D);
    rig.Frame({ .held = Button::kA }, 4);
    Check(rig.model.HighlightedRow() == Row::GameLength, "the highlight stays while A is held");
    rig.Frame({}, 4);
    Check(rig.model.HighlightedRow() == Row::Scenario, "and follows the cursor again after");
}

void TestRowsOpenTheirScreens() {
    struct Case {
        int32_t v;
        Next next;
    };
    const Case cases[] = {
        { 0x8D, Next::ScenarioSelect }, { 0xA1, Next::StageSelect },    { 0xDD, Next::CharacterSelect },
        { 0xF1, Next::Handicap },       { 0x105, Next::ControlStyle },
    };
    for (const Case& c : cases) {
        Rig rig;
        rig.Open(4);
        Check(Is(rig.Choose(c.v, 4), c.next), "the row opens its screen");
        Check(rig.model.Sfx().size() == 1 && rig.model.Sfx()[0] == FolderSfx::DoorMetalClose2, "with the door sound");
    }
}

void TestInPlaceRows() {
    Rig rig;
    rig.Open(4);
    const MpGameLength length = rig.Settings().gameLength;
    Check(rig.Choose(0xB5, 4).empty() && rig.Settings().gameLength != length, "game length steps in place");
    rig.Frame({}, 4);
    const int weapons = rig.Settings().weaponSet;
    Check(rig.Choose(0xC9, 4).empty() && rig.Settings().weaponSet == (weapons + 1) % 0xE, "so do the weapons");
    rig.Frame({}, 4);
    Check(rig.Choose(0x119, 4).empty() && rig.Settings().aimSetting == 0, "and the aim");
    Check(rig.front.multiplayerPlayerAim[3].sight == 0 && rig.front.multiplayerPlayerAim[3].autoaim == 0,
          "which every player's data follows");
}

void TestStartTab() {
    Rig rig;
    rig.Open(4);
    rig.Settings().stage = MpStage::Caves;
    const auto next = rig.Frame(Press(Button::kStart), 4);
    Check(Is(next, Next::RunStage), "Start runs the stage");
    Check(rig.front.selectedStage == static_cast<int32_t>(Frigate::Level::LevelId::Caves), "the one chosen");
    Check(rig.front.briefingPage == -1, "with no briefing page");
    Check(rig.model.Sfx().size() == 1, "and a sound");

    Rig tab;
    tab.Open(2);
    tab.front.cursor.h = 400.0f;
    tab.front.cursor.v = 100.0f;
    tab.Frame({}, 2);
    Check(Is(tab.Frame(Press(Button::kA), 2), Next::RunStage), "so does A on the start tab");
    Check(tab.front.selectedStage == static_cast<int32_t>(Frigate::Level::LevelId::Temple), "Temple by default");
}

void TestRandomStageHonoursTheUnlocksAndPlayerCount() {
    for (int players = 2; players <= 4; players++) {
        Rig rig;
        rig.Open(players);
        rig.Settings().stage = MpStage::Random;
        // Draws land on stages 11 (Egypt), 7 (Facility), 9 (Archives), then 3 (Caves).
        rig.draws = { 10, 6, 8, 2 };
        const auto next = rig.Frame(Press(Button::kStart), players);
        Check(Is(next, Next::RunStage), "random runs a stage");
        Check(rig.front.selectedStage == static_cast<int32_t>(Frigate::Level::LevelId::Caves),
              "stages the folder has not opened are drawn again");
        Check(rig.draw == 4, "one draw each");
        Check(rig.Settings().stage == MpStage::Random, "and the choice stays Random");
    }

    Rig open;
    OpenEveryStage(open);
    open.Open(2);
    open.Settings().stage = MpStage::Random;
    open.draws = { 10 };
    open.Frame(Press(Button::kStart), 2);
    Check(open.front.selectedStage == static_cast<int32_t>(Frigate::Level::LevelId::Egypt),
          "two players can draw Egypt once it is open");

    Rig three;
    OpenEveryStage(three);
    three.Open(3);
    three.Settings().stage = MpStage::Random;
    three.draws = { 10, 9 };
    three.Frame(Press(Button::kStart), 3);
    Check(three.front.selectedStage == static_cast<int32_t>(Frigate::Level::LevelId::Caverns),
          "three players skip Egypt for Caverns");
}

void TestBackTab() {
    Rig rig;
    rig.Open(3);
    const auto next = rig.Frame(Press(Button::kB), 3);
    Check(Is(next, Next::ModeSelect), "B goes back to the mode select");
    Check(rig.front.tabPrevSelected, "through the previous tab");
    Check(rig.front.cursor.h == 126.0f && rig.front.cursor.v == static_cast<float>(1 * 0x20 + 0xE2),
          "with the cursor on the multiplayer entry");
    Check(rig.model.Sfx().size() == 1 && rig.model.Sfx()[0] == FolderSfx::DoorMetalClose2, "and the door sound");

    Rig tab;
    tab.Open(3);
    tab.front.cursor.h = 400.0f;
    tab.front.cursor.v = 300.0f;
    tab.Frame({}, 3);
    Check(Is(tab.Frame(Press(Button::kZ), 3), Next::ModeSelect), "Z on the previous tab does too");
}

void TestStartWinsOverTheRowPress() {
    Rig rig;
    rig.Open(2);
    rig.Cursor(0xB5);
    rig.Frame({}, 2);
    const MpGameLength length = rig.Settings().gameLength;
    const auto next = rig.Frame(Press(Button::kStart | Button::kA), 2);
    Check(Is(next, Next::RunStage) && rig.Settings().gameLength == length, "Start takes the frame from A");
    Check(rig.model.Sfx().size() == 1, "with one sound");
}

void TestSettingsReachTheStage() {
    for (int players = 2; players <= 4; players++) {
        Rig rig;
        rig.Open(players);
        rig.Settings().stage = MpStage::Stack;
        rig.Settings().gameLength = MpGameLength::TwentyPoints;
        rig.Frame(Press(Button::kStart), players);
        const MultiplayerSettings& s = rig.front.multiplayer;
        Check(s.playerCount == players && s.PointLimit() == 20, "the stage reads the choices");
        Check(s.Valid(), "and they can start a game");
        Check(rig.front.selectedStage == static_cast<int32_t>(Frigate::Level::LevelId::Stack), "on the chosen stage");
    }
}

} // namespace

int main() {
    TestPlayerCountFollowsTheControllers();
    TestLaterVisitsKeepTheCount();
    TestUnplugPullsTheCountDown();
    TestTooFewControllersLeave();
    TestPlayersRowCycles();
    TestScenarioLimitsFollowThePlayerCount();
    TestStageFallsBackToFit();
    TestNewPlayersGetTheirOwnCharacter();
    TestControlStylesFitThePlayerCount();
    TestScenarioDefaults();
    TestGameLengthCycles();
    TestWeaponSetCycles();
    TestAimCycleAndCopy();
    TestFirstOpenReplacesALockedStage();
    TestInitClearsTheTabs();
    TestHighlightFollowsTheCursor();
    TestLockedRowsFallThrough();
    TestHeldButtonKeepsTheHighlight();
    TestRowsOpenTheirScreens();
    TestInPlaceRows();
    TestStartTab();
    TestRandomStageHonoursTheUnlocksAndPlayerCount();
    TestBackTab();
    TestStartWinsOverTheRowPress();
    TestSettingsReachTheStage();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    return 0;
}
