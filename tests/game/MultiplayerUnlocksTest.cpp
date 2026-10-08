#include <array>
#include <cstdio>
#include <memory>
#include <vector>

#include "game/FavouriteWeapons.h"
#include "game/MultiplayerUnlocks.h"
#include "game/save/SaveProgress.h"

using namespace Frigate::Game;
using namespace Frigate::Game::Save;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

struct Rig {
    std::shared_ptr<MemorySaveStorage> storage = std::make_shared<MemorySaveStorage>();
    std::shared_ptr<SaveManager> manager;
    std::unique_ptr<SaveProgress> progress;

    Rig() {
        manager = std::make_shared<SaveManager>(storage);
        manager->ValidateSaves();
        progress = std::make_unique<SaveProgress>(manager);
    }

    void Complete(int32_t first, int32_t last, int32_t difficulty = kDifficultyAgent) {
        for (int32_t level = first; level <= last; level++) {
            progress->UnlockStage(0, level, difficulty, 100 + level);
        }
    }
};

void TestFreshSaveOpensTheSevenDefaultStages() {
    Rig rig;
    for (int players = 1; players <= 4; players++) {
        for (int stage = 0; stage < 7; stage++) {
            Check(MultiplayerUnlocks::IsStageUnlocked(*rig.progress, stage, players), "a default stage is open");
        }
        for (int stage = 7; stage < kMpStageCount; stage++) {
            Check(!MultiplayerUnlocks::IsStageUnlocked(*rig.progress, stage, players),
                  "the progression stages are shut");
        }
    }
}

void TestProgressionStagesFollowTheirLevel() {
    Rig rig;
    rig.Complete(0, 0);
    Check(MultiplayerUnlocks::IsStageUnlocked(*rig.progress, 7, 2), "Facility opens with Facility");
    Check(!MultiplayerUnlocks::IsStageUnlocked(*rig.progress, 8, 2), "Bunker stays shut");

    rig.Complete(1, 7);
    Check(MultiplayerUnlocks::IsStageUnlocked(*rig.progress, 8, 2), "Bunker opens with Bunker II");
    Check(!MultiplayerUnlocks::IsStageUnlocked(*rig.progress, 9, 2), "Archives stays shut");

    rig.Complete(8, 9);
    Check(MultiplayerUnlocks::IsStageUnlocked(*rig.progress, 9, 2), "Archives opens with Archives");
    rig.Complete(10, 15);
    Check(MultiplayerUnlocks::IsStageUnlocked(*rig.progress, 10, 2), "Caverns opens with Caverns");
    Check(!MultiplayerUnlocks::IsStageUnlocked(*rig.progress, 11, 2), "Egypt needs Egypt");

    rig.progress->UnlockStage(0, kLevelEgypt, kDifficulty00, 300);
    Check(MultiplayerUnlocks::IsStageUnlocked(*rig.progress, 11, 2), "Egypt opens once the folder has beaten it");
}

void TestProgressIsPerFolder() {
    Rig rig;
    rig.Complete(0, 0);
    rig.progress->SetCurrentFolder(1);
    Check(!MultiplayerUnlocks::IsStageUnlocked(*rig.progress, 7, 2), "another folder has not unlocked Facility");
    rig.progress->SetCurrentFolder(0);
    Check(MultiplayerUnlocks::IsStageUnlocked(*rig.progress, 7, 2), "the first folder has");
}

void TestPlayerCountLimitsStages() {
    Rig rig;
    rig.Complete(0, 15);
    rig.progress->UnlockStage(0, kLevelEgypt, kDifficulty00, 300);

    const int maxPlayers[kMpStageCount] = { 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 2 };
    for (int stage = 0; stage < kMpStageCount; stage++) {
        for (int players = 1; players <= 4; players++) {
            Check(MultiplayerUnlocks::IsStageUnlocked(*rig.progress, stage, players) == (players <= maxPlayers[stage]),
                  "a stage is offered up to its player limit");
        }
    }
    Check(!MultiplayerUnlocks::IsStageUnlocked(*rig.progress, -1, 2), "a stage before the table is shut");
    Check(!MultiplayerUnlocks::IsStageUnlocked(*rig.progress, kMpStageCount, 2), "a stage past the table is shut");

    const auto three = MultiplayerUnlocks::UnlockedStages(*rig.progress, 3);
    Check(three[10] && !three[11], "three players get Caverns but not Egypt");
    const auto four = MultiplayerUnlocks::UnlockedStages(*rig.progress, 4);
    Check(four[7] && !four[8] && !four[9] && !four[10] && !four[11], "four players get only the four-player stages");
}

void TestStageRules() {
    Check(MultiplayerUnlocks::UnlockAfter(8) == 8, "Bunker follows Bunker II");
    Check(MultiplayerUnlocks::UnlockAfter(9) == 10, "Archives follows Archives");
    Check(MultiplayerUnlocks::UnlockAfter(10) == 16, "Caverns follows Caverns");
    Check(MultiplayerUnlocks::UnlockAfter(11) == kLevelEgypt, "Egypt follows Egypt");
    Check(MultiplayerUnlocks::UnlockAfter(99) == -1, "no rule past the table");
    Check(SelectedPlayerCount(true, 3) == 3, "multiplayer mode reports the selection");
    Check(SelectedPlayerCount(false, 3) == 1, "any other mode is one player");
}

void TestStageFallbacks() {
    Rig rig;
    Check(MultiplayerUnlocks::StageWhenOpeningOptions(*rig.progress, 7, 2) == kMpStageTemple,
          "a locked stage falls back to Temple");
    Check(MultiplayerUnlocks::StageWhenOpeningOptions(*rig.progress, 3, 2) == 3, "an open stage is kept");
    rig.Complete(0, 7);
    Check(MultiplayerUnlocks::StageWhenOpeningOptions(*rig.progress, 8, 3) == 8, "an unlocked stage is kept");
    Check(MultiplayerUnlocks::StageWhenOpeningOptions(*rig.progress, 8, 4) == kMpStageTemple,
          "four players cannot keep Bunker");

    Check(MultiplayerUnlocks::StageForPlayerCount(8, 4) == kMpStageTemple, "too few slots falls back to Temple");
    Check(MultiplayerUnlocks::StageForPlayerCount(8, 3) == 8, "three fit in Bunker");
    Check(MultiplayerUnlocks::StageForPlayerCount(11, 3) == kMpStageTemple, "three do not fit in Egypt");
    Check(MultiplayerUnlocks::StageForPlayerCount(11, 2) == 11, "two fit in Egypt");

    Check(MultiplayerUnlocks::HighlightFallback(*rig.progress, 11, 2) == 8,
          "the highlight steps back to an open stage");
    Check(MultiplayerUnlocks::HighlightFallback(*rig.progress, 11, 4) == 7, "four players step back to Facility");
    Check(MultiplayerUnlocks::HighlightFallback(*rig.progress, 5, 4) == 5, "an open highlight stays");
    Check(MultiplayerUnlocks::HighlightFallback(*rig.progress, 0, 2) == 0, "Random never steps back");
}

void TestRandomStageSkipsLockedStages() {
    Rig rig;
    std::vector<uint32_t> draws = { 6, 7, 10, 14 };
    size_t next = 0;
    int stage = MultiplayerUnlocks::ResolveRandomStage(*rig.progress, 4, [&] { return draws[next++]; });
    // Draws 6, 7 and 10 are Facility, Bunker and Egypt (shut); 14 % 11 = 3 is Library.
    Check(stage == 4 && next == 4, "locked draws are rerolled");

    next = 0;
    draws = { 0 };
    Check(MultiplayerUnlocks::ResolveRandomStage(*rig.progress, 2, [&] { return draws[next++]; }) == 1,
          "a draw of 0 is Temple, never Random");

    rig.progress->UnlockStage(0, kLevelEgypt, kDifficulty00, 1);
    draws = { 10 };
    next = 0;
    Check(MultiplayerUnlocks::ResolveRandomStage(*rig.progress, 2, [&] { return draws[next++]; }) == 11,
          "an unlocked Egypt can be drawn");
}

void TestCharacterCountOnAFreshSave() {
    Rig rig;
    MultiplayerUnlocks unlocks;
    Check(unlocks.NumCharsSelectable() == 8, "eight characters at the start");
    Check(unlocks.IsCharacterSelectable(7) && !unlocks.IsCharacterSelectable(8), "rows 0 to 7 are selectable");

    std::array<int, 4> chars{ 0, 9, 8, 30 };
    std::array<bool, 4> selected{};
    unlocks.OnCharacterSelectEntered(*rig.progress, 4, chars, selected);
    Check(unlocks.NumCharsSelectable() == 8, "still eight");
    Check(chars[0] == 0, "an in-range character is kept");
    Check(chars[1] == 0 && chars[2] == 0 && chars[3] == 0, "out of range characters move to the first row");
}

void TestCharacterMoveAvoidsLockedInCharacters() {
    Rig rig;
    MultiplayerUnlocks unlocks;
    std::array<int, 4> chars{ 0, 1, 20, 21 };
    std::array<bool, 4> selected{ true, true, false, false };
    unlocks.OnCharacterSelectEntered(*rig.progress, 4, chars, selected);
    // Players 0 and 1 hold rows 0 and 1, so player 2 takes row 2; player 3
    // then finds row 2 unclaimed (player 2 has not locked in) and takes it too.
    Check(chars[2] == 2 && chars[3] == 2, "only locked-in characters are skipped");

    chars = { 0, 1, 2, 40 };
    selected = { true, true, true, false };
    unlocks.OnCharacterSelectEntered(*rig.progress, 4, chars, selected);
    Check(chars[3] == 3, "the first row nobody holds");

    chars = { 5, -1, 8, -1 };
    selected = { false, false, false, false };
    unlocks.OnCharacterSelectEntered(*rig.progress, 4, chars, selected);
    Check(chars[0] == 5 && chars[1] == -1 && chars[3] == -1, "kept characters and the own-row marker stay");
    Check(chars[2] == 0, "player 3 moves to row 0");

    // Slots past selected_num_players still get a character, and do not count
    // as holding one.
    chars = { 0, 1, 9, 9 };
    selected = { true, true, true, true };
    unlocks.OnCharacterSelectEntered(*rig.progress, 2, chars, selected);
    Check(chars[2] == 2 && chars[3] == 2, "slots past the player count are moved but do not block");
}

void TestCradleOpensMoreCharactersAndTheyAreKept() {
    Rig rig;
    MultiplayerUnlocks unlocks;
    std::array<int, 4> chars{ 0, 1, 25, 32 };
    std::array<bool, 4> selected{};

    rig.Complete(0, 17);
    unlocks.OnCharacterSelectEntered(*rig.progress, 4, chars, selected);
    Check(unlocks.NumCharsSelectable() == 0x21, "Cradle opens 0x21 characters");
    Check(unlocks.IsCharacterSelectable(0x20) && !unlocks.IsCharacterSelectable(0x21), "rows 0 to 0x20");
    Check(chars[2] == 25 && chars[3] == 32, "characters are left alone when the count grows");

    rig.progress->SetCurrentFolder(1);
    unlocks.OnCharacterSelectEntered(*rig.progress, 4, chars, selected);
    Check(unlocks.NumCharsSelectable() == 8, "the count follows the current folder");
    Check(chars[2] == 0 && chars[3] == 0, "and characters past it move");
}

void TestCradleCompletedOnAHarderDifficultyCountsAtAgent() {
    Rig rig;
    MultiplayerUnlocks unlocks;
    std::array<int, 4> chars{ 0, 1, 2, 3 };
    std::array<bool, 4> selected{};
    rig.progress->UnlockStage(0, kLevelCradle, kDifficulty00, 100);
    unlocks.OnCharacterSelectEntered(*rig.progress, 2, chars, selected);
    Check(unlocks.NumCharsSelectable() == 0x21, "completing Cradle on a harder difficulty counts at Agent");
}

void TestCheatKeepsEveryCharacter() {
    Rig rig;
    MultiplayerUnlocks unlocks;
    std::array<int, 4> chars{ 0, 1, 50, 63 };
    std::array<bool, 4> selected{};
    unlocks.UnlockAllCharacters();
    Check(unlocks.NumCharsSelectable() == 0x40 && unlocks.IsCharacterSelectable(63), "the cheat opens all 64");
    unlocks.OnCharacterSelectEntered(*rig.progress, 4, chars, selected);
    Check(unlocks.NumCharsSelectable() == 0x40, "entering the select does not take them back");
    Check(chars[2] == 50 && chars[3] == 63, "characters are untouched");
    Check(!unlocks.IsCharacterSelectable(64) && !unlocks.IsCharacterSelectable(-1), "nothing past the table");

    MultiplayerUnlocks fresh;
    Check(fresh.NumCharsSelectable() == 8, "another run starts at eight");
}

void TestPlayersHaveSelectedChar() {
    const std::array<int, 4> chars{ 3, 4, 5, 6 };
    const std::array<bool, 4> selected{ true, false, true, true };
    Check(PlayersHaveSelectedChar(3, 4, chars, selected), "a locked-in character is held");
    Check(!PlayersHaveSelectedChar(4, 4, chars, selected), "an unlocked one is not");
    Check(PlayersHaveSelectedChar(5, 3, chars, selected), "player 3 counts with three players");
    Check(!PlayersHaveSelectedChar(6, 3, chars, selected), "player 4 does not count with three players");
    Check(PlayersHaveSelectedChar(6, 4, chars, selected), "and does with four");
}

void TestFavouriteWeaponsStartUnarmed() {
    FavouriteWeapons weapons;
    for (int player = 0; player < 4; player++) {
        Check(weapons.Weapon(player, FavouriteWeapons::kRight) == 0 &&
                  weapons.Weapon(player, FavouriteWeapons::kLeft) == 0,
              "every player starts unarmed");
        Check(!weapons.IsDualWielded(player), "unarmed is not dual wielding");
    }
}

void TestFavouriteWeaponsStoreEachPlayer() {
    FavouriteWeapons weapons;
    weapons.Store(2, 7, 9);
    Check(weapons.Weapon(2, 0) == 7 && weapons.Weapon(2, 1) == 9, "stored per hand");
    Check(weapons.Weapon(1, 0) == 0 && weapons.Weapon(3, 1) == 0, "other players are untouched");
    weapons.Store(2, 12, 0);
    Check(weapons.Weapon(2, 0) == 12 && weapons.Weapon(2, 1) == 0, "a new store replaces both hands");
    weapons.Store(4, 1, 1);
    weapons.Store(-1, 1, 1);
    Check(weapons.Weapon(4, 0) == 0 && weapons.Weapon(2, 2) == 0 && weapons.Weapon(-1, 0) == 0,
          "out of range is ignored");
}

void TestSetForEveryPlayerCoversTwoThreeAndFour() {
    for (int count = 1; count <= 4; count++) {
        FavouriteWeapons weapons;
        std::vector<int> asked;
        weapons.StoreForEveryPlayer(count, [&](int player) {
            asked.push_back(player);
            return std::array<int32_t, 2>{ 10 + player, 20 + player };
        });
        Check(static_cast<int>(asked.size()) == count, "one query per player");
        for (int player = 0; player < 4; player++) {
            const bool stored = player < count;
            Check(weapons.Weapon(player, 0) == (stored ? 10 + player : 0), "right hand stored for players in the game");
            Check(weapons.Weapon(player, 1) == (stored ? 20 + player : 0), "left hand stored for players in the game");
        }
    }

    FavouriteWeapons weapons;
    weapons.Store(3, 5, 5);
    weapons.StoreForEveryPlayer(2, [](int) { return std::array<int32_t, 2>{ 1, 2 }; });
    Check(weapons.Weapon(3, 0) == 5, "a player who was not in the game keeps the last game's weapon");
    weapons.StoreForEveryPlayer(4, [](int) { return std::array<int32_t, 2>{ 0, 0 }; });
    Check(weapons.Weapon(3, 0) == 0, "a player who held nothing overwrites it with unarmed");
}

void TestDualWielded() {
    FavouriteWeapons weapons;
    weapons.Store(0, 7, 7);
    weapons.Store(1, 7, 8);
    weapons.Store(2, 7, 0);
    weapons.Store(3, 0, 0);
    Check(weapons.IsDualWielded(0), "the same weapon in both hands");
    Check(!weapons.IsDualWielded(1) && !weapons.IsDualWielded(2) && !weapons.IsDualWielded(3), "anything else is not");
}

} // namespace

int main() {
    TestFreshSaveOpensTheSevenDefaultStages();
    TestProgressionStagesFollowTheirLevel();
    TestProgressIsPerFolder();
    TestPlayerCountLimitsStages();
    TestStageRules();
    TestStageFallbacks();
    TestRandomStageSkipsLockedStages();
    TestCharacterCountOnAFreshSave();
    TestCharacterMoveAvoidsLockedInCharacters();
    TestCradleOpensMoreCharactersAndTheyAreKept();
    TestCradleCompletedOnAHarderDifficultyCountsAtAgent();
    TestCheatKeepsEveryCharacter();
    TestPlayersHaveSelectedChar();
    TestFavouriteWeaponsStartUnarmed();
    TestFavouriteWeaponsStoreEachPlayer();
    TestSetForEveryPlayerCoversTwoThreeAndFour();
    TestDualWielded();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    return 0;
}
