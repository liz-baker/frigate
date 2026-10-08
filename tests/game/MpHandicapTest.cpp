#include "game/menu/MpHandicapModel.h"

#include <array>
#include <cstdio>

#include "game/FrontState.h"
#include "game/Input.h"

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
    FrontState front;
    MpHandicapModel model;
    Inputs inputs{};

    explicit Rig(int players) {
        front.gameMode = GameMode::Multi;
        front.multiplayer = MultiplayerSettings::Defaults(players);
        model.Init(front);
    }
    bool Frame(int player, const Input::Sample& input = {}) {
        inputs = {};
        inputs[static_cast<size_t>(player)] = input;
        return model.Update(inputs, front);
    }
    bool Idle() {
        inputs = {};
        return model.Update(inputs, front);
    }
};

void TestLeftAndRightStepTheRow() {
    Rig rig(2);
    Check(rig.front.multiplayer.handicap[0] == 5, "the screen starts on the normal handicap");
    rig.Frame(0, Press(Button::kPadRight));
    Check(rig.front.multiplayer.handicap[0] == 6, "right steps to the easier handicap");
    rig.Frame(0, Press(Button::kCRight));
    Check(rig.front.multiplayer.handicap[0] == 7, "C right does too");
    rig.Frame(0, Press(Button::kPadLeft));
    rig.Frame(0, Press(Button::kCLeft));
    rig.Frame(0, Press(Button::kPadLeft));
    Check(rig.front.multiplayer.handicap[0] == 4, "left steps to the harder one");
    Check(rig.front.multiplayer.handicap[1] == 5, "the other player's row is untouched");
}

void TestTheRowStopsAtEitherEnd() {
    Rig rig(2);
    for (int i = 0; i < 20; i++) {
        rig.Frame(1, Press(Button::kPadRight));
    }
    Check(rig.front.multiplayer.handicap[1] == MpHandicapModel::kLastRow, "right stops at the last row");
    for (int i = 0; i < 20; i++) {
        rig.Frame(1, Press(Button::kPadLeft));
    }
    Check(rig.front.multiplayer.handicap[1] == 0, "left stops at the first row");
}

void TestTheStickStepsOncePerPush() {
    Rig rig(2);
    // The latch starts open only once the stick has been seen near the centre.
    rig.Frame(0, Stick(100));
    Check(rig.front.multiplayer.handicap[0] == 5, "a push before the stick was centred does nothing");
    rig.Idle();
    rig.Frame(0, Stick(100));
    Check(rig.front.multiplayer.handicap[0] == 6, "a push after centring steps right");
    rig.Frame(0, Stick(100));
    rig.Frame(0, Stick(100));
    Check(rig.front.multiplayer.handicap[0] == 6, "holding it does not step again");
    rig.Idle();
    rig.Frame(0, Stick(-100));
    Check(rig.front.multiplayer.handicap[0] == 5, "a push left steps left");
    rig.Idle();
    rig.Frame(0, Stick(60));
    Check(rig.front.multiplayer.handicap[0] == 6, "a push of 60 steps");
    rig.Idle();
    rig.Frame(0, Stick(-15));
    Check(rig.front.multiplayer.handicap[0] == 6, "a slight push left does not");
}

void TestButtonsWinOverTheStickAndEachOtherInOrder() {
    Rig rig(2);
    rig.Idle();
    Input::Sample both = Press(Button::kPadLeft | Button::kPadRight);
    rig.Frame(0, both);
    Check(rig.front.multiplayer.handicap[0] == 4, "left is checked before right");
}

void TestLockInRaisesTheSound() {
    Rig rig(2);
    rig.Frame(0, Press(Button::kA));
    Check(rig.front.playerHasSelectedChar[0], "A locks the player in");
    Check(rig.model.Sfx().size() == 1 && rig.model.Sfx()[0] == FolderSfx::DoorMetalClose2, "with the door sound");
    rig.Frame(1, Press(Button::kZ));
    Check(rig.front.playerHasSelectedChar[1], "Z does too");
    rig.Frame(0, Press(Button::kPadRight));
    Check(rig.front.multiplayer.handicap[0] == 5, "a locked-in player's row cannot change");
}

void TestStartLocksIn() {
    Rig rig(2);
    rig.Frame(0, Press(Button::kStart));
    Check(rig.front.playerHasSelectedChar[0], "Start locks the player in");
}

void TestBackUnlocks() {
    Rig rig(2);
    rig.Frame(0, Press(Button::kA));
    rig.Frame(0, Press(Button::kB));
    Check(!rig.front.playerHasSelectedChar[0], "B takes the lock-in back");
    Check(rig.model.Sfx().size() == 1, "with a sound");
    rig.Frame(1, Press(Button::kB));
    Check(rig.model.Sfx().empty(), "B does nothing for a player who has not locked in");
}

void TestBackAndLockInInOneFrame() {
    Rig rig(2);
    rig.Frame(0, Press(Button::kA));
    rig.Frame(0, Press(Button::kB | Button::kA));
    Check(rig.front.playerHasSelectedChar[0], "B then A in the same frame locks in again");
    Check(rig.model.Sfx().size() == 2, "with a sound for each");
}

void TestEveryPlayerMustLockIn() {
    for (int players = 2; players <= 4; players++) {
        Rig rig(players);
        bool done = false;
        for (int p = 0; p < players; p++) {
            Check(!done, "not done before the last player");
            done = rig.Frame(p, Press(Button::kA));
        }
        Check(done, "done once every player has locked in");
        rig.Frame(players - 1, Press(Button::kB));
        Check(!rig.Idle(), "not done when one takes it back");
    }
}

void TestOnlyPlayersInTheGameCount() {
    Rig rig(3);
    rig.Frame(3, Press(Button::kA));
    Check(!rig.front.playerHasSelectedChar[3], "a fourth controller does nothing in a three player game");
    rig.Frame(0, Press(Button::kA));
    rig.Frame(1, Press(Button::kA));
    Check(rig.Frame(2, Press(Button::kA)), "three lock-ins finish a three player game");
}

void TestEachPlayerReadsTheirOwnController() {
    Rig rig(4);
    rig.inputs[2] = Press(Button::kPadLeft);
    rig.inputs[3] = Press(Button::kPadRight);
    rig.model.Update(rig.inputs, rig.front);
    Check(rig.front.multiplayer.handicap[0] == 5 && rig.front.multiplayer.handicap[1] == 5,
          "the first two are untouched");
    Check(rig.front.multiplayer.handicap[2] == 4, "the third player's left stepped their row");
    Check(rig.front.multiplayer.handicap[3] == 6, "the fourth player's right stepped theirs");
}

void TestInitClearsTheLockIns() {
    Rig rig(2);
    rig.Frame(0, Press(Button::kA));
    rig.front.tabPrevSelected = true;
    rig.model.Init(rig.front);
    Check(!rig.front.playerHasSelectedChar[0], "init clears the lock-ins");
    Check(!rig.front.tabPrevSelected, "and the tab flags");
    Check(rig.front.multiplayer.handicap[0] == 5, "but keeps the handicap");
}

void TestFirstControllerMovesTheCursor() {
    Rig rig(2);
    const float before = rig.front.cursor.h;
    Input::Sample first = Stick(80);
    first.prevStickX = 0;
    rig.Frame(0, first);
    Check(rig.front.cursor.h != before, "the first controller's stick moves the cursor");
}

void TestHandicapTable() {
    MultiplayerSettings settings;
    settings.handicap[0] = 0;
    settings.handicap[1] = MpHandicapModel::kLastRow;
    settings.handicap[2] = MultiplayerSettings::kNormalHandicap;
    Check(settings.Handicap(0) == 10.0f, "the first row takes ten times the damage");
    Check(settings.Handicap(1) < 0.11f && settings.Handicap(1) > 0.09f, "the last takes a tenth");
    Check(settings.Handicap(2) == 1.0f, "the normal handicap takes the damage as it comes");
}

} // namespace

int main() {
    TestLeftAndRightStepTheRow();
    TestTheRowStopsAtEitherEnd();
    TestTheStickStepsOncePerPush();
    TestButtonsWinOverTheStickAndEachOtherInOrder();
    TestLockInRaisesTheSound();
    TestStartLocksIn();
    TestBackUnlocks();
    TestBackAndLockInInOneFrame();
    TestEveryPlayerMustLockIn();
    TestOnlyPlayersInTheGameCount();
    TestEachPlayerReadsTheirOwnController();
    TestInitClearsTheLockIns();
    TestFirstControllerMovesTheCursor();
    TestHandicapTable();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    return 0;
}
