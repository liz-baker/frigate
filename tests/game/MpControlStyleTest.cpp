#include "game/menu/MpControlStyleModel.h"

#include <array>
#include <cstdio>

#include "game/FrontState.h"
#include "game/Input.h"

using namespace Frigate::Game;
using namespace Frigate::Game::Menu;
using Frigate::Level::ControlConfig;
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
    MpControlStyleModel model;
    Inputs inputs{};

    explicit Rig(int players) {
        front.gameMode = GameMode::Multi;
        front.multiplayer.playerCount = players;
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
    ControlConfig& Style(int player) {
        return front.multiplayer.controlStyle[static_cast<size_t>(player)];
    }
    void Right(int player, int times = 1) {
        for (int i = 0; i < times; i++) {
            Frame(player, Press(Button::kPadRight));
        }
    }
    void Left(int player, int times = 1) {
        for (int i = 0; i < times; i++) {
            Frame(player, Press(Button::kPadLeft));
        }
    }
};

void TestEveryPlayerStepsThroughTheOneControllerStyles() {
    for (int players = 2; players <= 4; players++) {
        Rig rig(players);
        for (int p = (players == 2 ? 1 : 0); p < players; p++) {
            Check(rig.Style(p) == ControlConfig::Honey, "a player starts on 1.1");
            rig.Right(p, 2);
            Check(rig.Style(p) == ControlConfig::Kissy, "right twice reaches 1.3");
            rig.Left(p);
            Check(rig.Style(p) == ControlConfig::Solitare, "left goes back to 1.2");
            rig.Right(p, 10);
            Check(rig.Style(p) == ControlConfig::Goodnight, "right stops at 1.4");
            rig.Left(p, 10);
            Check(rig.Style(p) == ControlConfig::Honey, "left stops at 1.1");
        }
    }
}

void TestTheSecondPlayerOfTwoCannotLeaveTheOneControllerStyles() {
    Rig rig(2);
    rig.Right(1, 10);
    Check(rig.Style(1) == ControlConfig::Goodnight, "the second player stops at 1.4");
    Check(rig.Style(0) == ControlConfig::Honey, "and the first player is untouched");
}

void TestThreeAndFourPlayersNeverReachTheTwoControllerStyles() {
    for (int players = 3; players <= 4; players++) {
        Rig rig(players);
        for (int p = 0; p < players; p++) {
            rig.Right(p, 10);
            Check(rig.Style(p) == ControlConfig::Goodnight, "right stops at 1.4 with three or more players");
        }
    }
}

void TestTheFirstPlayerOfTwoStepsIntoTheTwoControllerStyles() {
    Rig rig(2);
    rig.Right(0, 3);
    Check(rig.Style(0) == ControlConfig::Goodnight, "1.4 comes before the two controller styles");
    rig.Right(0);
    Check(rig.Style(0) == ControlConfig::Plenty, "right from 1.4 reaches 2.1");
    Check(rig.Style(1) == ControlConfig::Plenty, "and takes the second player's pad as 2.1");
    rig.Right(0, 10);
    Check(rig.Style(0) == ControlConfig::Goodhead, "right stops at 2.4");
    Check(rig.Style(1) == ControlConfig::Plenty, "the second player stays on 2.1");
}

void TestTheSecondPlayerFollowsTheTwoControllerStylesUp() {
    Rig rig(2);
    rig.Right(0, 4);
    rig.Right(1, 10);
    Check(rig.Style(1) == ControlConfig::Goodhead, "the second player can step up through 2.1 to 2.4");
    rig.Left(1, 10);
    Check(rig.Style(1) == ControlConfig::Plenty, "and not below 2.1 while the first player is on one");
}

void TestTheFirstPlayerLeavingTheTwoControllerStylesResetsTheSecond() {
    Rig rig(2);
    rig.Right(0, 4);
    rig.Right(1, 2);
    rig.Left(0);
    Check(rig.Style(0) == ControlConfig::Goodnight, "left from 2.1 reaches 1.4");
    Check(rig.Style(1) == ControlConfig::Honey, "and puts the second player back on 1.1");
}

void TestTheFirstPlayerCanStepBetweenTwoControllerStyles() {
    Rig rig(2);
    rig.Right(0, 5);
    Check(rig.Style(0) == ControlConfig::Galore, "2.2");
    rig.Left(0);
    Check(rig.Style(0) == ControlConfig::Plenty, "left steps back down to 2.1");
    Check(rig.Style(1) == ControlConfig::Plenty, "with the second player kept on one");
}

void TestTheStickStepsOncePerPush() {
    Rig rig(2);
    rig.Idle();
    rig.Frame(0, Stick(100));
    rig.Frame(0, Stick(100));
    Check(rig.Style(0) == ControlConfig::Solitare, "holding the stick steps once");
    rig.Idle();
    rig.Frame(0, Stick(-100));
    Check(rig.Style(0) == ControlConfig::Honey, "pushing left steps back");
}

void TestLockInAndBack() {
    Rig rig(2);
    rig.Frame(0, Press(Button::kA));
    Check(rig.front.playerHasSelectedChar[0], "A locks in");
    Check(rig.model.Sfx().size() == 1 && rig.model.Sfx()[0] == FolderSfx::DoorMetalClose2, "with the door sound");
    rig.Right(0);
    Check(rig.Style(0) == ControlConfig::Honey, "a locked-in player cannot step");
    rig.Frame(0, Press(Button::kB));
    Check(!rig.front.playerHasSelectedChar[0], "B takes it back");
    rig.Frame(1, Press(Button::kZ));
    Check(rig.front.playerHasSelectedChar[1], "Z locks in");
    rig.Frame(1, Press(Button::kB));
    rig.Frame(1, Press(Button::kStart));
    Check(rig.front.playerHasSelectedChar[1], "Start locks in");
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
    }
}

void TestEachPlayerReadsTheirOwnController() {
    Rig rig(4);
    rig.inputs[2] = Press(Button::kPadRight);
    rig.inputs[3] = Press(Button::kPadRight);
    rig.model.Update(rig.inputs, rig.front);
    Check(rig.Style(0) == ControlConfig::Honey && rig.Style(1) == ControlConfig::Honey, "the first two are untouched");
    Check(rig.Style(2) == ControlConfig::Solitare && rig.Style(3) == ControlConfig::Solitare, "the others stepped");
}

void TestInitClearsTheLockIns() {
    Rig rig(2);
    rig.Frame(0, Press(Button::kA));
    rig.model.Init(rig.front);
    Check(!rig.front.playerHasSelectedChar[0], "init clears the lock-ins");
}

} // namespace

int main() {
    TestEveryPlayerStepsThroughTheOneControllerStyles();
    TestTheSecondPlayerOfTwoCannotLeaveTheOneControllerStyles();
    TestThreeAndFourPlayersNeverReachTheTwoControllerStyles();
    TestTheFirstPlayerOfTwoStepsIntoTheTwoControllerStyles();
    TestTheSecondPlayerFollowsTheTwoControllerStylesUp();
    TestTheFirstPlayerLeavingTheTwoControllerStylesResetsTheSecond();
    TestTheFirstPlayerCanStepBetweenTwoControllerStyles();
    TestTheStickStepsOncePerPush();
    TestLockInAndBack();
    TestEveryPlayerMustLockIn();
    TestEachPlayerReadsTheirOwnController();
    TestInitClearsTheLockIns();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    return 0;
}
