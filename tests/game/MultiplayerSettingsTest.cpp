#include "game/MultiplayerSettings.h"

#include <cmath>
#include <cstdio>

using namespace Frigate::Game;
using Frigate::Level::ControlConfig;
using Frigate::Level::LevelId;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool Near(float a, float b) {
    return std::fabs(a - b) < 1e-6f;
}

void TestDefaults() {
    for (int players = 1; players <= 4; players++) {
        const MultiplayerSettings settings = MultiplayerSettings::Defaults(players);
        Check(settings.playerCount == players, "defaults keep the player count");
        Check(settings.Valid() || players == 1, "defaults for 2 to 4 players are valid");
        for (size_t player = 0; player < 4; player++) {
            Check(settings.character[player] == static_cast<int>(player),
                  "each player starts with their own character");
            Check(Near(settings.Handicap(static_cast<int>(player)), 1.0f), "the default handicap is normal");
            Check(settings.controlStyle[player] == ControlConfig::Honey, "the default control style is 1.1");
        }
    }
    const MultiplayerSettings settings;
    Check(settings.scenario == MpScenario::Normal && settings.stage == MpStage::Temple, "scenario and stage");
    Check(settings.gameLength == MpGameLength::TenMinutes && settings.TimeLimit() == 36000,
          "ten minutes is 36000 ticks");
    Check(settings.PointLimit() == 0, "a timed game has no point limit");
    Check(settings.aimSetting == 3 && settings.Aim().autoaim == 1 && settings.Aim().sight == 1, "aim defaults");
    Check(settings.weaponSet == 0xB, "the default weapon set");
    Check(settings.StageLevel() == LevelId::Temple, "temple");
}

void TestLimits() {
    MultiplayerSettings settings;
    settings.gameLength = MpGameLength::FivePoints;
    Check(settings.TimeLimit() == 0 && settings.PointLimit() == 5, "first to 5");
    settings.gameLength = MpGameLength::TwentyPoints;
    Check(settings.PointLimit() == 20, "first to 20");
    settings.gameLength = MpGameLength::TwentyMinutes;
    Check(settings.TimeLimit() == 72000, "twenty minutes");
    settings.gameLength = MpGameLength::LastAlive;
    Check(settings.TimeLimit() == 0 && settings.PointLimit() == 0, "last alive has neither");
}

void TestTables() {
    MultiplayerSettings settings;
    settings.handicap = { 0, 10, 4, 6 };
    Check(Near(settings.Handicap(0), 10.0f) && Near(settings.Handicap(1), 0.1f), "handicap table ends");
    Check(Near(settings.Handicap(2), 1.3f) && Near(settings.Handicap(3), 0.76923078f), "handicap table middle");
    settings.character = { 10, 33, -1, 63 };
    Check(Near(settings.CharacterHeight(0), 0.78780001f), "oddjob");
    Check(Near(settings.CharacterHeight(1), 0.88529998f), "rosika");
    Check(Near(settings.CharacterHeight(2), 1.0f), "an unset character uses the player's own row (player 2: 1.0)");
    Check(Near(settings.CharacterHeight(3), 1.0446f), "the last character");
    Check(settings.CharacterBody(0) == "BODY_Oddjob" && settings.CharacterHead(0) == "HEAD_Male_Brosnan_Tuxedo" &&
              !settings.CharacterIsFemale(0),
          "oddjob's body, head and gender");
    Check(settings.CharacterBody(1) == "BODY_Rosika" && settings.CharacterHead(1) == "HEAD_Female_Marion_Rosika" &&
              settings.CharacterIsFemale(1),
          "rosika's");
    Check(settings.CharacterBody(2) == "BODY_Trevelyan_Janus" && !settings.CharacterIsFemale(2),
          "an unset character takes the player's own row (player 3: Trevelyan)");
    Check(settings.CharacterBody(3) == "BODY_Brosnan_Tuxedo" && settings.CharacterHead(3) == "HEAD_Female_Vivien" &&
              settings.CharacterIsFemale(3),
          "the last row is a head on Brosnan's body");
    settings.character = { -1, -1, -1, -1 };
    Check(Near(settings.CharacterHeight(1), 0.96609998f), "an unset character uses the player's own row");
    for (int row = 0; row < MultiplayerSettings::kCharacterRows; row++) {
        settings.character[0] = row;
        Check(settings.CharacterHeight(0) > 0.5f, "every character row has a height");
    }
}

void TestTeams() {
    MultiplayerSettings settings = MultiplayerSettings::Defaults(4);
    Check(!settings.IsTeamScenario() && settings.TeamFlag(1) == 0, "no teams in the normal scenario");
    settings.scenario = MpScenario::TwoVsTwo;
    settings.teamSize = 2;
    Check(settings.IsTeamScenario(), "2v2 has teams");
    Check(settings.TeamFlag(0) == 0 && settings.TeamFlag(2) == 0 && settings.TeamFlag(1) == 1 &&
              settings.TeamFlag(3) == 1,
          "2v2 pairs the first player with the one teamsize names");
    settings.scenario = MpScenario::ThreeVsOne;
    settings.teamSize = 3;
    Check(settings.TeamFlag(3) == 1 && settings.TeamFlag(0) == 0 && settings.TeamFlag(2) == 0, "3v1");
}

void TestValid() {
    const char* reason = nullptr;
    MultiplayerSettings settings = MultiplayerSettings::Defaults(2);
    Check(settings.Valid(&reason) && reason == nullptr, "two players");
    settings.playerCount = 5;
    Check(!settings.Valid(&reason) && reason != nullptr, "five players");
    settings.playerCount = 0;
    Check(!settings.Valid(), "no players");
    settings = MultiplayerSettings::Defaults(3);
    settings.scenario = MpScenario::TwoVsTwo;
    Check(!settings.Valid(), "2v2 needs four");
    settings.scenario = MpScenario::TwoVsOne;
    Check(settings.Valid(), "2v1 takes three");
    settings.playerCount = 4;
    Check(!settings.Valid(), "2v1 takes only three");
    settings.scenario = MpScenario::Normal;
    settings.stage = MpStage::Egypt;
    Check(!settings.Valid(), "egypt takes two");
    settings.playerCount = 2;
    Check(settings.Valid(), "egypt with two");
    settings.handicap[1] = 11;
    Check(!settings.Valid(), "a handicap row past the table");
    settings.handicap[1] = 5;
    settings.character[0] = 64;
    Check(!settings.Valid(), "a character row past the table");
    settings.character[0] = 0;
    settings.controlStyle[1] = ControlConfig::Cinema;
    Check(!settings.Valid(), "cinema is no player's control style");
    settings.controlStyle[1] = ControlConfig::Goodhead;
    Check(settings.Valid(), "goodhead is");
    settings.weaponSet = 14;
    Check(!settings.Valid(), "a weapon set past the table");
    Check(MultiplayerSettings::StagePlayers(MpStage::Bunker).max == 3, "bunker takes three");
    Check(MultiplayerSettings::ScenarioPlayers(MpScenario::TwoVsOne).min == 3, "2v1 range");
    settings = MultiplayerSettings::Defaults(2);
    settings.stage = MpStage::Random;
    Check(!settings.StageLevel().has_value(), "a random stage has no level yet");
}

} // namespace

int main() {
    TestDefaults();
    TestLimits();
    TestTables();
    TestTeams();
    TestValid();
    if (gFailures == 0) {
        std::printf("all multiplayer settings tests passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
