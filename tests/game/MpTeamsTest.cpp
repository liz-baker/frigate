#include "game/menu/MpTeamsModel.h"

#include <cstdio>

#include "game/FrontState.h"
#include "game/Input.h"

using namespace Frigate::Game;
using namespace Frigate::Game::Menu;
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

struct Rig {
    FrontState front;
    MpTeamsModel model;

    Rig(MpScenario scenario, int players) {
        front.gameMode = GameMode::Multi;
        front.multiplayer.playerCount = players;
        front.multiplayer.scenario = scenario;
        model.Init(front);
    }
    bool Frame(const Input::Sample& input = {}) {
        return model.Update(input, front);
    }
    int TeamSize() const {
        return front.multiplayer.teamSize;
    }
    bool Cued() const {
        return model.Sfx().size() == 1 && model.Sfx()[0] == FolderSfx::DoorMetalClose2;
    }
};

constexpr uint16_t kRight = Button::kPadRight;
constexpr uint16_t kLeft = Button::kPadLeft;
constexpr uint16_t kUp = Button::kPadUp;
constexpr uint16_t kDown = Button::kPadDown;

void TestInitStartsAtTheLastPlayerOrTheThirdForTwoVsOne() {
    Check(Rig(MpScenario::TwoVsTwo, 4).TeamSize() == 3, "2v2 starts with the fourth player as the partner");
    Check(Rig(MpScenario::ThreeVsOne, 4).TeamSize() == 3, "3v1 starts with the fourth player alone");
    Check(Rig(MpScenario::TwoVsOne, 3).TeamSize() == 2, "2v1 starts with the third player alone");
}

void TestTwoVsTwoMovesThePartnerAroundTheOtherThreePanels() {
    Rig rig(MpScenario::TwoVsTwo, 4);
    // 3 is the bottom right panel.
    rig.Frame(Press(kLeft));
    Check(rig.TeamSize() == 2 && rig.Cued(), "left from the bottom right goes to the bottom left, with a sound");
    rig.Frame(Press(kDown));
    Check(rig.TeamSize() == 2 && rig.model.Sfx().empty(), "down from the bottom left does nothing");
    rig.Frame(Press(kUp));
    Check(rig.TeamSize() == 1 && rig.Cued(), "up from the bottom left goes to the top right: the top left is player one");
    rig.Frame(Press(kRight));
    Check(rig.TeamSize() == 1 && rig.model.Sfx().empty(), "right from the right column does nothing");
    rig.Frame(Press(kLeft));
    Check(rig.TeamSize() == 2, "left from the top right goes to the bottom left in 2v2");
    rig.Frame(Press(kRight));
    Check(rig.TeamSize() == 3, "right goes to the bottom right");
    rig.Frame(Press(kUp));
    Check(rig.TeamSize() == 1, "up goes to the top right");
    rig.Frame(Press(kDown));
    Check(rig.TeamSize() == 3, "down from the top right goes to the bottom right");
    rig.Frame(Press(kDown));
    Check(rig.TeamSize() == 3 && rig.model.Sfx().empty(), "down from the bottom does nothing");
}

void TestTwoVsTwoFlags() {
    Rig rig(MpScenario::TwoVsTwo, 4);
    // The partner is the fourth player: the first and fourth are one team.
    Check(rig.front.multiplayer.TeamFlag(0) == 0, "player one is on the first team");
    Check(rig.front.multiplayer.TeamFlag(1) == 1, "player two on the second");
    Check(rig.front.multiplayer.TeamFlag(2) == 1, "player three on the second");
    Check(rig.front.multiplayer.TeamFlag(3) == 0, "player four, the partner, on the first");
    rig.Frame(Press(kLeft));
    Check(rig.front.multiplayer.TeamFlag(2) == 0 && rig.front.multiplayer.TeamFlag(3) == 1,
          "moving the partner moves the first team");
}

void TestThreeVsOneLoner() {
    Rig rig(MpScenario::ThreeVsOne, 4);
    rig.Frame(Press(kLeft));
    Check(rig.TeamSize() == 2 && rig.Cued(), "left moves the loner one panel left");
    rig.Frame(Press(kUp));
    Check(rig.TeamSize() == 0 && rig.Cued(), "up moves the loner up a row, onto the first panel");
    rig.Frame(Press(kUp));
    Check(rig.TeamSize() == 0 && rig.model.Sfx().empty(), "up from the top does nothing");
    rig.Frame(Press(kRight));
    Check(rig.TeamSize() == 1, "right moves the loner right");
    rig.Frame(Press(kDown));
    Check(rig.TeamSize() == 3, "down moves the loner down");
    Check(rig.front.multiplayer.TeamFlag(3) == 1 && rig.front.multiplayer.TeamFlag(0) == 0 &&
              rig.front.multiplayer.TeamFlag(1) == 0 && rig.front.multiplayer.TeamFlag(2) == 0,
          "only the loner is on the second team");
}

void TestTwoVsOneLoner() {
    Rig rig(MpScenario::TwoVsOne, 3);
    Check(rig.TeamSize() == 2, "starts on the third player");
    rig.Frame(Press(kRight));
    Check(rig.TeamSize() == 1 && rig.Cued(), "right goes to the second player in 2v1");
    rig.Frame(Press(kRight));
    Check(rig.TeamSize() == 1 && rig.model.Sfx().empty(), "right again does nothing");
    rig.Frame(Press(kDown));
    Check(rig.TeamSize() == 2 && rig.Cued(), "down goes to the third player");
    rig.Frame(Press(kDown));
    Check(rig.TeamSize() == 2 && rig.model.Sfx().empty(), "down again does nothing");
    rig.Frame(Press(kUp));
    Check(rig.TeamSize() == 0 && rig.Cued(), "up goes to the first player");
    rig.Frame(Press(kRight));
    Check(rig.TeamSize() == 1, "right from the first goes to the second");
    rig.Frame(Press(kLeft));
    Check(rig.TeamSize() == 0, "left goes back to the first");
    Check(rig.front.multiplayer.TeamFlag(0) == 1 && rig.front.multiplayer.TeamFlag(1) == 0 &&
              rig.front.multiplayer.TeamFlag(2) == 0,
          "only the first player is on the second team");
}

void TestCButtonsAndTheStickMoveToo() {
    Rig rig(MpScenario::ThreeVsOne, 4);
    rig.Frame(Press(Button::kCLeft));
    Check(rig.TeamSize() == 2, "C left moves");
    rig.Frame(Press(Button::kCUp));
    Check(rig.TeamSize() == 0, "C up moves");
    rig.Frame(Press(Button::kCRight));
    Check(rig.TeamSize() == 1, "C right moves");
    rig.Frame(Press(Button::kCDown));
    Check(rig.TeamSize() == 3, "C down moves");

    Input::Sample left = { .stickX = -100 };
    rig.Frame(left);
    Check(rig.TeamSize() == 2, "the stick moves left with no latch");
    Input::Sample up = { .stickY = 100 };
    rig.Frame(up);
    Check(rig.TeamSize() == 0, "the stick moves up");
    Input::Sample right = { .stickX = 100 };
    rig.Frame(right);
    Check(rig.TeamSize() == 1, "the stick moves right");
    Input::Sample down = { .stickY = -100 };
    rig.Frame(down);
    Check(rig.TeamSize() == 3, "the stick moves down");

    Input::Sample slight = { .stickX = -15, .stickY = 15 };
    rig.Frame(slight);
    Check(rig.TeamSize() == 3, "a slight push does not");
}

void TestAcceptingGoesBack() {
    Rig rig(MpScenario::TwoVsTwo, 4);
    Check(!rig.Frame(), "nothing is accepted on its own");
    Check(rig.Frame(Press(Button::kA)), "A accepts");
    Check(rig.Cued(), "with the door sound");
    Check(rig.Frame(Press(Button::kZ)), "Z accepts");
    Check(rig.Frame(Press(Button::kStart)), "Start accepts");
    Check(!rig.Frame(Press(Button::kB)), "B does not");
}

void TestAMoveAndAcceptInOneFrameKeepsTheMove() {
    Rig rig(MpScenario::ThreeVsOne, 4);
    Check(rig.Frame(Press(kLeft | Button::kA)), "left and A together accept");
    Check(rig.TeamSize() == 2, "after the move");
    Check(rig.model.Sfx().size() == 2, "with a sound for each");
}

void TestTheLonerFlashes() {
    Rig rig(MpScenario::ThreeVsOne, 4);
    int shown = 0;
    for (int frame = 0; frame < 40; frame++) {
        rig.Frame();
        Check(rig.model.TextShown(rig.front, 0), "another panel always shows");
        if (rig.model.TextShown(rig.front, 3)) {
            shown++;
        }
    }
    Check(shown == 20, "the loner's text is shown half the frames");

    Rig fresh(MpScenario::ThreeVsOne, 4);
    fresh.Frame();
    Check(fresh.model.TextShown(fresh.front, 3), "it starts shown");
    for (int i = 0; i < 9; i++) {
        fresh.Frame();
    }
    Check(!fresh.model.TextShown(fresh.front, 3), "and is hidden from the tenth frame on");
    for (int i = 0; i < 10; i++) {
        fresh.Frame();
    }
    Check(fresh.model.TextShown(fresh.front, 3), "and back at the twentieth");
}

void TestInitRestartsTheFlash() {
    Rig rig(MpScenario::ThreeVsOne, 4);
    for (int i = 0; i < 15; i++) {
        rig.Frame();
    }
    rig.model.Init(rig.front);
    rig.Frame();
    Check(rig.model.TextShown(rig.front, 3), "init restarts the flash");
}

void TestFirstControllerMovesTheCursor() {
    Rig rig(MpScenario::TwoVsTwo, 4);
    const float before = rig.front.cursor.h;
    Input::Sample stick = { .stickX = 80 };
    rig.Frame(stick);
    Check(rig.front.cursor.h != before, "the stick moves the cursor");
}

} // namespace

int main() {
    TestInitStartsAtTheLastPlayerOrTheThirdForTwoVsOne();
    TestTwoVsTwoMovesThePartnerAroundTheOtherThreePanels();
    TestTwoVsTwoFlags();
    TestThreeVsOneLoner();
    TestTwoVsOneLoner();
    TestCButtonsAndTheStickMoveToo();
    TestAcceptingGoesBack();
    TestAMoveAndAcceptInOneFrameKeepsTheMove();
    TestTheLonerFlashes();
    TestInitRestartsTheFlash();
    TestFirstControllerMovesTheCursor();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    return 0;
}
