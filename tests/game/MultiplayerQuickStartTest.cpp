#include "game/MultiplayerQuickStart.h"

#include <cstdio>
#include <set>

using namespace Frigate::Game;
using Frigate::Level::ControlConfig;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

QuickStartArgs Args(int players) {
    QuickStartArgs args;
    args.players = players;
    return args;
}

void TestDefaults() {
    for (int players = 2; players <= 4; players++) {
        std::string error;
        const auto settings = ParseQuickStart(Args(players), &error);
        Check(settings.has_value(), "the defaults start a game at every player count");
        if (settings) {
            Check(settings->playerCount == players, "the player count is kept");
            Check(settings->stage == MpStage::Temple && settings->scenario == MpScenario::Normal,
                  "the defaults are the settings' own");
        }
    }
    std::string error;
    Check(!ParseQuickStart(Args(5), &error) && !error.empty(), "five players are refused");
    Check(!ParseQuickStart(Args(0), &error), "no players are refused");
}

void TestNames() {
    // Every name is accepted, spelled with any case and punctuation, and so is its row number.
    for (int row = 0; row < static_cast<int>(MpStage::Count); row++) {
        QuickStartArgs args = Args(2);
        args.stage = StageName(static_cast<MpStage>(row));
        const auto byName = ParseQuickStart(args);
        args.stage = std::to_string(row);
        const auto byRow = ParseQuickStart(args);
        Check(byName && byRow && byName->stage == static_cast<MpStage>(row) && byRow->stage == byName->stage,
              "a stage by name or row");
    }
    for (int row = 0; row < static_cast<int>(MpScenario::Count); row++) {
        const MpScenario scenario = static_cast<MpScenario>(row);
        // The smallest and the largest game the scenario allows.
        for (const int players : { MultiplayerSettings::ScenarioPlayers(scenario).min,
                                   MultiplayerSettings::ScenarioPlayers(scenario).max }) {
            QuickStartArgs args = Args(players);
            args.scenario = ScenarioName(scenario);
            const auto settings = ParseQuickStart(args);
            Check(settings && settings->scenario == scenario, "a scenario by name");
        }
    }
    QuickStartArgs args = Args(3);
    args.stage = "  TEMPLE ";
    Check(ParseQuickStart(args) && ParseQuickStart(args)->stage == MpStage::Temple, "spelling is forgiven");
    args.stage = "nowhere";
    std::string error;
    Check(!ParseQuickStart(args, &error) && error.find("stage") != std::string::npos, "an unknown stage is named");
    args = Args(2);
    args.gameLength = "20points";
    Check(ParseQuickStart(args) && ParseQuickStart(args)->PointLimit() == 20, "a game length");
    args = Args(2);
    args.weaponSet = "rockets";
    Check(ParseQuickStart(args) && ParseQuickStart(args)->weaponSet == 11, "a weapon set");
    for (int set = 0; set < MultiplayerSettings::kWeaponSets; set++) {
        args.weaponSet = WeaponSetName(set);
        Check(ParseQuickStart(args) && ParseQuickStart(args)->weaponSet == set, "every weapon set by name");
    }
    args.weaponSet = "99";
    Check(!ParseQuickStart(args), "a weapon set past the table");
}

void TestPerPlayerLists() {
    QuickStartArgs args = Args(4);
    args.characters = "10,11,12,13";
    args.handicaps = "0,5,10,5";
    args.controlStyles = "1.1,1.2,2.3,2.4";
    auto settings = ParseQuickStart(args);
    Check(settings.has_value(), "per-player lists");
    if (settings) {
        Check(settings->character[3] == 13 && settings->handicap[2] == 10, "the lists land on their players");
        Check(settings->controlStyle[0] == ControlConfig::Honey &&
                  settings->controlStyle[1] == ControlConfig::Solitare &&
                  settings->controlStyle[3] == static_cast<ControlConfig>(7),
              "control styles");
    }
    args.characters = "7";
    settings = ParseQuickStart(args);
    Check(settings && settings->character[0] == 7 && settings->character[3] == 7, "one value covers every player");
    args.characters = "1,2,3";
    std::string error;
    Check(!ParseQuickStart(args, &error) && error.find("characters") != std::string::npos,
          "a list of the wrong length is refused");
    args.characters = "";
    args.handicaps = "11,5,5,5";
    Check(!ParseQuickStart(args), "a handicap past the table");
    args.handicaps = "";
    args.controlStyles = "3.1,1.1,1.1,1.1";
    Check(!ParseQuickStart(args), "a control style that is not one");
    // Only the first `players` entries matter.
    args = Args(2);
    args.characters = "-1,5";
    settings = ParseQuickStart(args);
    Check(settings && settings->character[0] == -1 && settings->character[1] == 5, "-1 is the player's own row");
}

void TestTeams() {
    std::string error;
    Check(TeamSizeFor(MpScenario::TwoVsTwo, 4, "AABB", &error) == 1, "2 vs 2: players 1 and 2 together");
    Check(TeamSizeFor(MpScenario::TwoVsTwo, 4, "ABAB") == 2, "2 vs 2: players 1 and 3 together");
    Check(TeamSizeFor(MpScenario::TwoVsTwo, 4, "BAAB") == 3, "2 vs 2: the letters' own names don't matter");
    Check(!TeamSizeFor(MpScenario::TwoVsTwo, 4, "AAAB", &error), "2 vs 2 needs two a side");
    Check(!TeamSizeFor(MpScenario::TwoVsTwo, 4, "AAB", &error), "one letter per player");
    Check(!TeamSizeFor(MpScenario::TwoVsTwo, 4, "AACC", &error), "only A and B");
    Check(TeamSizeFor(MpScenario::ThreeVsOne, 4, "AAAB") == 3, "3 vs 1: the lone player is the one");
    Check(TeamSizeFor(MpScenario::ThreeVsOne, 4, "BAAA") == 0, "3 vs 1: the lone player can be player 1");
    Check(!TeamSizeFor(MpScenario::ThreeVsOne, 4, "AABB", &error), "3 vs 1 needs one alone");
    Check(TeamSizeFor(MpScenario::TwoVsOne, 3, "ABA") == 1, "2 vs 1");
    Check(!TeamSizeFor(MpScenario::TwoVsOne, 3, "AAA", &error), "2 vs 1 can't have everyone together");

    // The teams reach the flags the team select stores.
    QuickStartArgs args = Args(4);
    args.scenario = "2v2";
    args.teams = "ABAB";
    const auto settings = ParseQuickStart(args);
    Check(settings.has_value(), "a team game");
    if (settings) {
        Check(settings->TeamFlag(0) == settings->TeamFlag(2) && settings->TeamFlag(1) == settings->TeamFlag(3) &&
                  settings->TeamFlag(0) != settings->TeamFlag(1),
              "the flags follow the letters");
    }
    args.scenario = "normal";
    Check(!ParseQuickStart(args, &error), "teams in a scenario without them");
}

void TestCheats() {
    // Every cheat a game can switch on is accepted by name and by id.
    for (int32_t cheat = 1; CheatName(cheat) != nullptr; cheat++) {
        QuickStartArgs args = Args(3);
        args.cheats = CheatName(cheat);
        const auto byName = ParseQuickStart(args);
        args.cheats = std::to_string(cheat);
        const auto byId = ParseQuickStart(args);
        Check(byName && byId && byName->cheats == std::vector<int32_t>{ cheat } && byId->cheats == byName->cheats,
              "a cheat by name or id");
    }
    Check(CheatName(0) == nullptr && CheatName(35) == nullptr, "ids outside the gameplay cheats have no name");

    QuickStartArgs args = Args(4);
    args.cheats = "Invincible, no-radar,invincible,DK_MODE";
    const auto settings = ParseQuickStart(args);
    Check(settings && settings->cheats.size() == 3 && settings->cheats[0] == 2 && settings->cheats[1] == 23 &&
              settings->cheats[2] == 12,
          "a list keeps its order, ignores case and punctuation and drops a repeat");
    std::string error;
    args.cheats = "invincible,nonsense";
    Check(!ParseQuickStart(args, &error) && error.find("nonsense") != std::string::npos,
          "an unknown cheat is refused by name");
    args.cheats = "99";
    Check(!ParseQuickStart(args, &error), "so is an id past the gameplay cheats");
    Check(ParseQuickStart(Args(2))->cheats.empty(), "no cheats by default");
}

void TestValidation() {
    std::string error;
    QuickStartArgs args = Args(2);
    args.scenario = "2v2";
    Check(!ParseQuickStart(args, &error) && !error.empty(), "a scenario the player count doesn't fit");
    args = Args(4);
    args.stage = "egypt";
    Check(!ParseQuickStart(args, &error), "a stage the player count doesn't fit");
    args = Args(3);
    args.stage = "egypt";
    Check(!ParseQuickStart(args, &error), "egypt takes two players");
    args = Args(2);
    args.stage = "egypt";
    Check(ParseQuickStart(args).has_value(), "egypt for two");
}

void TestRandomStage() {
    for (int players = 2; players <= 4; players++) {
        std::set<MpStage> seen;
        for (uint32_t random = 0; random < 40; random++) {
            MultiplayerSettings settings = MultiplayerSettings::Defaults(players);
            settings.stage = MpStage::Random;
            ResolveRandomStage(settings, random);
            Check(settings.stage != MpStage::Random && settings.Valid(), "a random stage fits the players");
            seen.insert(settings.stage);
        }
        Check(seen.size() > 1, "random reaches more than one stage");
        Check(players == 4 ? seen.count(MpStage::Egypt) == 0 && seen.count(MpStage::Caverns) == 0
                           : seen.count(MpStage::Temple) == 1,
              "random keeps to the stages the count allows");
    }
    MultiplayerSettings fixed = MultiplayerSettings::Defaults(2);
    fixed.stage = MpStage::Stack;
    ResolveRandomStage(fixed, 3);
    Check(fixed.stage == MpStage::Stack, "a chosen stage is left alone");
}

void TestPorts() {
    Check(PortsForPlayers(1) == 0x1 && PortsForPlayers(2) == 0x3 && PortsForPlayers(3) == 0x7 &&
              PortsForPlayers(4) == 0xF,
          "players 1 to n are the ports forced");
    Check(PortsForPlayers(0) == 0 && PortsForPlayers(9) == 0xF, "out-of-range counts are clamped");
}

} // namespace

int main() {
    TestDefaults();
    TestNames();
    TestPerPlayerLists();
    TestCheats();
    TestTeams();
    TestValidation();
    TestRandomStage();
    TestPorts();
    if (gFailures == 0) {
        std::printf("all tests passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
