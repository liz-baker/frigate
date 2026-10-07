#include "game/menu/MpCharSelectModel.h"

#include <array>
#include <cstdio>
#include <memory>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MultiplayerCharacters.h"
#include "game/save/SaveProgress.h"

using namespace Frigate::Game;
using namespace Frigate::Game::Menu;
namespace Button = Frigate::Game::Input::Button;
using Inputs = std::array<Input::Sample, kMpMaxPlayers>;

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

Input::Sample Stick(int8_t x) {
    return { .stickX = x };
}

struct Rig {
    std::shared_ptr<Save::MemorySaveStorage> storage = std::make_shared<Save::MemorySaveStorage>();
    std::shared_ptr<Save::SaveManager> manager;
    FrontState front;
    MpCharSelectModel model;
    Inputs inputs{};

    explicit Rig(int players) {
        manager = std::make_shared<Save::SaveManager>(storage);
        manager->ValidateSaves();
        front.progress = std::make_shared<Save::SaveProgress>(manager);
        front.progress->SetCurrentFolder(0);
        front.gameMode = GameMode::Multi;
        front.multiplayer.playerCount = players;
        model.Init(front);
    }

    // One frame with `input` on controller `player` and nothing on the rest.
    bool Frame(int player, const Input::Sample& input = {}) {
        inputs = {};
        inputs[static_cast<size_t>(player)] = input;
        return model.Update(inputs, front);
    }
    bool Idle() {
        inputs = {};
        return model.Update(inputs, front);
    }
    // Frames until every strip has stopped scrolling.
    void Settle() {
        for (int i = 0; i < 60; i++) {
            Idle();
        }
    }
    // Presses A for `player` and runs the lock-in animation to its end.
    bool Lock(int player) {
        Frame(player, Press(Button::kA));
        bool ready = false;
        for (int i = 0; i < 12; i++) {
            ready = Idle();
        }
        return ready;
    }
};

void TestStickRange() {
    using M = MpCharSelectModel;
    Check(M::StickXInRange(0) == -1, "a centred stick reads -1");
    Check(M::StickXInRange(19) == -1, "a slight push right still reads -1");
    Check(M::StickXInRange(20) == 0, "past a third of the way right reads 0");
    Check(M::StickXInRange(59) == 0, "just short of the right reads 0");
    Check(M::StickXInRange(60) == 1, "60 and up reads 1");
    Check(M::StickXInRange(127) == 1, "all the way right reads 1");
    Check(M::StickXInRange(-20) == -1, "-20 reads -1");
    Check(M::StickXInRange(-21) == -2, "-21 and past reads -2");
    Check(M::StickXInRange(-128) == -2, "all the way left reads -2");
}

void TestInitStartsOnEachPlayersCharacter() {
    for (int players = 2; players <= 4; players++) {
        Rig rig(players);
        rig.front.multiplayer.character = { 5, 3, 7, 1 };
        rig.model.Init(rig.front);
        for (int p = 0; p < kMpMaxPlayers; p++) {
            const int expected = rig.front.multiplayer.character[static_cast<size_t>(p)];
            Check(rig.model.Cursor(p) == expected && rig.model.Previous(p) == expected,
                  "every strip starts on the player's character");
            Check(rig.model.ScrollOffset(p) == 0 && rig.model.ImageSize(p) == 0, "unscrolled and unlocked");
            Check(!rig.front.playerHasSelectedChar[static_cast<size_t>(p)], "nothing locked in");
        }
        Check(rig.model.NumCharacters() == 8, "a fresh save offers the eight Bond characters");
    }
}

void TestInitMovesAPlayerWhoseCharacterIsNoLongerOffered() {
    Rig rig(4);
    rig.front.multiplayer.character = { 0, 20, 2, 30 };
    rig.front.playerHasSelectedChar = { true, false, false, false };
    rig.model.Init(rig.front);
    Check(rig.front.multiplayer.character[1] == 1, "the first row nobody has locked in");
    Check(rig.front.multiplayer.character[3] == 1, "the second such player is moved the same way");
    Check(rig.model.Cursor(1) == 1, "and the strip starts there");
    Check(!rig.front.playerHasSelectedChar[0], "locks from the last visit are cleared after the move");
}

void TestInitOffersMoreAfterCradle() {
    Rig rig(2);
    rig.front.progress->UnlockStage(0, Save::kLevelCradle, Save::kDifficultyAgent, 100);
    rig.model.Init(rig.front);
    Check(rig.model.NumCharacters() == 0x21, "completing Cradle at Agent offers 0x21");
    rig.front.mpUnlocks.UnlockAllCharacters();
    rig.model.Init(rig.front);
    Check(rig.model.NumCharacters() == 0x40, "the cheat offers all 64");
}

void TestCursorMovesOneCharacterAndScrolls() {
    Rig rig(2);
    rig.Frame(0, Press(Button::kPadRight));
    Check(rig.model.Cursor(0) == 1, "right moves the cursor on");
    Check(rig.model.Previous(0) == 0 && rig.model.ScrollOffset(0) == 12, "the strip starts scrolling by 12");
    for (int frame = 0; frame < 6; frame++) {
        rig.Idle();
    }
    Check(rig.model.Previous(0) == 1 && rig.model.ScrollOffset(0) == 0, "after seven frames it has arrived");
    Check(rig.model.Cursor(1) == rig.front.multiplayer.character[1], "the other strips stay put");

    rig.Frame(0, Press(Button::kCLeft));
    Check(rig.model.Cursor(0) == 0, "C left moves back");
    rig.Settle();
    Check(rig.model.Previous(0) == 0 && rig.model.ScrollOffset(0) == 0, "and the strip returns");

    rig.Frame(0, Press(Button::kPadLeft));
    Check(rig.model.Cursor(0) == 0, "the cursor stops at the first character");
    rig.Frame(0, Press(Button::kCRight));
    Check(rig.model.Cursor(0) == 1, "C right moves on");
}

void TestCursorStopsAtTheLastCharacter() {
    Rig rig(2);
    for (int i = 0; i < 12; i++) {
        rig.Frame(0, Press(Button::kPadRight));
    }
    Check(rig.model.Cursor(0) == 7, "eight characters end at row 7");
    rig.Settle();
    Check(rig.model.Previous(0) == 7, "the strip follows");

    rig.front.mpUnlocks.UnlockAllCharacters();
    rig.model.Init(rig.front);
    for (int i = 0; i < 100; i++) {
        rig.Frame(0, Press(Button::kPadRight));
    }
    Check(rig.model.Cursor(0) == 63, "all 64 end at row 63");
}

void TestStickMovesTheCursorOnlyWhileNotScrolling() {
    Rig rig(2);
    rig.Frame(0, Stick(80));
    Check(rig.model.Cursor(0) == 1, "the stick right moves on");
    rig.Frame(0, Stick(80));
    Check(rig.model.Cursor(0) == 1, "and not again while the strip is still scrolling");
    rig.Settle();
    rig.Frame(0, Stick(-40));
    Check(rig.model.Cursor(0) == 0, "the stick left moves back once settled");
    rig.Settle();
    rig.Frame(0, Stick(40));
    Check(rig.model.Cursor(0) == 0, "a half push right is not enough");
    rig.Frame(0, Stick(-10));
    Check(rig.model.Cursor(0) == 0, "nor a light push left");
}

void TestButtonsWinOverTheStick() {
    Rig rig(2);
    Input::Sample input = Press(Button::kPadLeft);
    input.stickX = 100;
    rig.Frame(0, input);
    Check(rig.model.Cursor(0) == 0, "a pressed left button is handled before the stick");
}

void TestLockInRaisesTheSoundAndGrowsThePortrait() {
    Rig rig(2);
    rig.Frame(0, Press(Button::kA));
    Check(rig.front.multiplayer.character[0] == 0 && rig.front.playerHasSelectedChar[0], "A locks in the cursor");
    Check(rig.model.ImageSize(0) == 1, "the portrait starts growing at 1");
    Check(rig.model.Sfx().size() == 1 && rig.model.Sfx()[0] == FolderSfx::DoorMetalClose2, "with the door sound");
    for (int i = 0; i < 12; i++) {
        rig.Idle();
        Check(rig.model.Sfx().empty(), "the sound is raised once");
    }
    Check(rig.model.ImageSize(0) == MpCharSelectModel::kFullSize, "it stops at 0xb");

    rig.Frame(0, Press(Button::kPadRight));
    Check(rig.model.Cursor(0) == 0, "a locked in strip ignores the cursor buttons");
}

void TestEveryLockInButtonWorks() {
    for (uint16_t button : { Button::kA, Button::kZ, Button::kStart }) {
        Rig rig(2);
        rig.Frame(1, Press(button));
        Check(rig.front.playerHasSelectedChar[1], "A, Z and Start all lock in");
    }
}

void TestBackUnlocksAndShrinksThePortrait() {
    Rig rig(2);
    rig.Lock(0);
    rig.Frame(0, Press(Button::kB));
    Check(!rig.front.playerHasSelectedChar[0], "B unlocks");
    Check(rig.model.Sfx().size() == 1 && rig.model.Sfx()[0] == FolderSfx::DoorMetalClose2, "with the door sound");
    Check(rig.model.ImageSize(0) == MpCharSelectModel::kFullSize - 1, "and the portrait starts shrinking");
    Check(rig.front.multiplayer.character[0] == 0, "the player's character stays as it was");
    for (int i = 0; i < 12; i++) {
        rig.Idle();
    }
    Check(rig.model.ImageSize(0) == 0, "until it is back to 0");

    rig.Frame(0, Press(Button::kB));
    Check(rig.model.Sfx().empty() && !rig.front.playerHasSelectedChar[0], "B with nothing locked in does nothing");
}

void TestACharacterAnotherPlayerHasTakenCannotBeChosen() {
    for (int players = 2; players <= 4; players++) {
        Rig rig(players);
        rig.front.multiplayer.character = { 0, 0, 0, 0 };
        rig.model.Init(rig.front);
        rig.Frame(0, Press(Button::kA));
        for (int other = 1; other < players; other++) {
            rig.Frame(other, Press(Button::kA));
            Check(!rig.front.playerHasSelectedChar[static_cast<size_t>(other)], "a taken character is refused");
            Check(rig.model.Sfx().empty() && rig.model.ImageSize(other) == 0, "silently");
        }
        rig.Frame(0, Press(Button::kB));
        rig.Frame(1, Press(Button::kA));
        Check(rig.front.playerHasSelectedChar[1], "once released it can be taken by another");
    }
}

void TestPlayerCanStepPastATakenCharacter() {
    Rig rig(3);
    rig.front.multiplayer.character = { 0, 0, 0, 0 };
    rig.model.Init(rig.front);
    rig.Frame(2, Press(Button::kA));
    rig.Frame(1, Press(Button::kPadRight));
    rig.Frame(1, Press(Button::kA));
    Check(rig.front.playerHasSelectedChar[1] && rig.front.multiplayer.character[1] == 1,
          "the next character is free to take");
    Check(rig.front.multiplayer.character[2] == 0, "the first player's is unchanged");
}

void TestOnlyPlayersInTheGameBlockACharacter() {
    Rig rig(2);
    rig.front.multiplayer.character = { 0, 1, 2, 0 };
    rig.front.playerHasSelectedChar[3] = true;
    rig.Frame(0, Press(Button::kA));
    Check(rig.front.playerHasSelectedChar[0], "a stale lock in a slot past the player count does not block");
}

void TestEachPlayerReadsTheirOwnController() {
    Rig rig(4);
    rig.inputs = {};
    rig.inputs[2] = Press(Button::kPadRight);
    rig.inputs[3] = Press(Button::kPadRight);
    rig.model.Update(rig.inputs, rig.front);
    Check(rig.model.Cursor(0) == 0 && rig.model.Cursor(1) == 1, "the first two did not move");
    Check(rig.model.Cursor(2) == 3 && rig.model.Cursor(3) == 4, "the last two moved");
}

void TestSelectionIsDoneOnlyWhenEveryPlayerIsLocked() {
    for (int players = 2; players <= 4; players++) {
        Rig rig(players);
        for (int p = 0; p < players - 1; p++) {
            Check(!rig.Lock(p), "not done while a player has yet to choose");
        }
        rig.Frame(players - 1, Press(Button::kA));
        Check(!rig.model.Update(Inputs{}, rig.front), "nor while the last portrait is still growing");
        bool done = false;
        for (int i = 0; i < 12; i++) {
            done = rig.Idle();
        }
        Check(done, "done once the last portrait is full");
        for (int p = 0; p < players; p++) {
            Check(rig.front.multiplayer.character[static_cast<size_t>(p)] == p, "each player has their own character");
        }
    }
}

void TestOneReleasingStopsTheScreenLeaving() {
    Rig rig(3);
    rig.Lock(0);
    rig.Lock(1);
    rig.Frame(2, Press(Button::kA));
    rig.Frame(0, Press(Button::kB));
    for (int i = 0; i < 20; i++) {
        Check(!rig.Idle(), "a released player keeps the screen open");
    }
}

void TestFirstControllerStickMovesTheCursor() {
    Rig rig(2);
    const float before = rig.front.cursor.h;
    rig.Frame(0, Stick(100));
    Check(rig.front.cursor.h > before, "controller 1's stick moves the menu cursor");
    const float after = rig.front.cursor.h;
    rig.Frame(1, Stick(100));
    Check(rig.front.cursor.h == after, "another controller's does not");
}

void TestTheCharactersTableCoversTheRoster() {
    Check(kMpCharacters.size() == 64, "64 rows");
    Check(kMpCharacters[0].nameSlot == 184 && kMpCharacters[0].photo == 0, "Bond");
    Check(kMpCharacters[7].nameSlot == 185 && kMpCharacters[7].photo == 15, "Mishkin has the sixteenth portrait");
    Check(kMpCharacters[63].nameSlot == 226 && kMpCharacters[63].photo == 14, "the last is the unknown portrait");
    for (const MpCharacterEntry& entry : kMpCharacters) {
        Check(entry.photo < kMpPortraitCount, "every portrait is one of the sixteen");
    }
}

} // namespace

int main() {
    TestStickRange();
    TestInitStartsOnEachPlayersCharacter();
    TestInitMovesAPlayerWhoseCharacterIsNoLongerOffered();
    TestInitOffersMoreAfterCradle();
    TestCursorMovesOneCharacterAndScrolls();
    TestCursorStopsAtTheLastCharacter();
    TestStickMovesTheCursorOnlyWhileNotScrolling();
    TestButtonsWinOverTheStick();
    TestLockInRaisesTheSoundAndGrowsThePortrait();
    TestEveryLockInButtonWorks();
    TestBackUnlocksAndShrinksThePortrait();
    TestACharacterAnotherPlayerHasTakenCannotBeChosen();
    TestPlayerCanStepPastATakenCharacter();
    TestOnlyPlayersInTheGameBlockACharacter();
    TestEachPlayerReadsTheirOwnController();
    TestSelectionIsDoneOnlyWhenEveryPlayerIsLocked();
    TestOneReleasingStopsTheScreenLeaving();
    TestFirstControllerStickMovesTheCursor();
    TestTheCharactersTableCoversTheRoster();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    return 0;
}
