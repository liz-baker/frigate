#include "game/menu/MpStageSelectModel.h"

#include <array>
#include <cstdio>
#include <memory>
#include <utility>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/MpStageSelectLayout.h"
#include "game/save/SaveProgress.h"

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
    std::shared_ptr<Save::MemorySaveStorage> storage = std::make_shared<Save::MemorySaveStorage>();
    std::shared_ptr<Save::SaveManager> manager;
    FrontState front;
    MpStageSelectModel model;

    explicit Rig(int players, bool everyStageOpen = true) {
        manager = std::make_shared<Save::SaveManager>(storage);
        manager->ValidateSaves();
        front.progress = std::make_shared<Save::SaveProgress>(manager);
        front.progress->SetCurrentFolder(0);
        front.gameMode = GameMode::Multi;
        front.multiplayer.playerCount = players;
        if (everyStageOpen) {
            for (int32_t level = 0; level <= 20; level++) {
                front.progress->UnlockStage(0, level, Save::kDifficultyAgent, 100);
            }
        }
        model.Init(front);
    }

    // The centre of a grid cell, in the cursor's screen space.
    void CursorOn(int row, int column) {
        const int32_t tops[3] = { 100, MpStageSelectModel::kSecondRowTop, MpStageSelectModel::kThirdRowTop };
        front.cursor.h = static_cast<float>(MpStageSelectModel::kColumnLeft[column] + 10);
        front.cursor.v = static_cast<float>(tops[row] + 5);
    }
    bool Frame(const Input::Sample& input = {}) {
        return model.Update(input, front);
    }
};

void TestInitHighlightsRandomAndClearsTheTabs() {
    Rig rig(2);
    rig.front.tabNextSelected = true;
    rig.front.tabPrevSelected = true;
    rig.CursorOn(2, 3);
    rig.Frame();
    rig.model.Init(rig.front);
    Check(rig.model.Highlighted() == static_cast<int>(MpStage::Random), "Random is highlighted at first");
    Check(!rig.front.tabNextSelected && !rig.front.tabPrevSelected && !rig.model.TabPrevHighlighted(),
          "no tab is chosen or highlighted");
}

void TestTheCursorPicksTheCell() {
    Rig rig(2);
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 4; column++) {
            rig.CursorOn(row, column);
            rig.Frame();
            Check(rig.model.Highlighted() == (row * 4) + column, "the cell under the cursor is highlighted");
        }
    }
    // The edges of the grid belong to the cell they open.
    rig.front.cursor = { 121.0f, 169.0f };
    rig.Frame();
    Check(rig.model.Highlighted() == 0, "one short of the second column and row is the first cell");
    rig.front.cursor = { 122.0f, 170.0f };
    rig.Frame();
    Check(rig.model.Highlighted() == 5, "the second column and row start at 122 and 170");
    rig.front.cursor = { 292.0f, 240.0f };
    rig.Frame();
    Check(rig.model.Highlighted() == 11, "the fourth column and third row start at 292 and 240");
}

void TestLockedStagesFallBackToTheEarlierOpenOne() {
    // Four players: the three-player stages and Egypt are out of reach.
    Rig four(4);
    four.CursorOn(2, 0);
    four.Frame();
    Check(four.model.Highlighted() == static_cast<int>(MpStage::Facility), "Facility takes four players");
    four.CursorOn(2, 1);
    four.Frame();
    Check(four.model.Highlighted() == static_cast<int>(MpStage::Facility), "Bunker takes three, so falls back");
    four.CursorOn(2, 3);
    four.Frame();
    Check(four.model.Highlighted() == static_cast<int>(MpStage::Facility), "and so do Archives to Egypt");

    Rig three(3);
    three.CursorOn(2, 3);
    three.Frame();
    Check(three.model.Highlighted() == static_cast<int>(MpStage::Caverns), "Caverns takes three players");

    Rig two(2);
    two.CursorOn(2, 3);
    two.Frame();
    Check(two.model.Highlighted() == static_cast<int>(MpStage::Egypt), "Egypt takes two");

    // A folder that has not unlocked the stages behind the first seven.
    Rig locked(2, false);
    locked.CursorOn(2, 3);
    locked.Frame();
    Check(locked.model.Highlighted() == static_cast<int>(MpStage::Stack), "a locked folder stops at Stack");
}

void TestAHeldButtonFreezesTheHighlight() {
    for (const uint16_t button : { Button::kA, Button::kZ, Button::kStart }) {
        Rig rig(2);
        rig.CursorOn(1, 1);
        rig.Frame();
        rig.CursorOn(2, 2);
        rig.Frame({ .held = button });
        Check(rig.model.Highlighted() == 5, "the highlight stays while a select button is held");
    }
    Rig rig(2);
    rig.CursorOn(0, 1);
    rig.Frame({ .held = Button::kB });
    Check(rig.model.Highlighted() == 1, "B held does not freeze it");
}

void TestChoosingStoresTheStageAndLeaves() {
    for (const uint16_t button : { Button::kA, Button::kZ, Button::kStart }) {
        Rig rig(3);
        rig.CursorOn(1, 3);
        rig.Frame();
        const bool done = rig.Frame(Press(button));
        Check(done, "the screen is done");
        Check(rig.front.multiplayer.stage == MpStage::Facility, "the highlighted stage is chosen");
        Check(rig.front.tabNextSelected && !rig.front.tabPrevSelected, "the next tab is chosen");
        Check(rig.model.Sfx().size() == 1 && rig.model.Sfx()[0] == FolderSfx::DoorMetalClose2, "the door sounds");
    }
}

void TestRandomIsChosenAsRandom() {
    Rig rig(2);
    rig.front.multiplayer.stage = MpStage::Temple;
    rig.CursorOn(0, 0);
    rig.Frame();
    rig.Frame(Press(Button::kA));
    Check(rig.front.multiplayer.stage == MpStage::Random, "Random is kept as Random until the game starts");
}

void TestChoosingALockedCellChoosesTheFallback() {
    Rig rig(4);
    rig.CursorOn(2, 3);
    rig.Frame();
    rig.Frame(Press(Button::kA));
    Check(rig.front.multiplayer.stage == MpStage::Facility, "the stage shown highlighted is the one chosen");
}

void TestPreviousTabAndBackLeaveWithoutChoosing() {
    Rig rig(2);
    rig.front.multiplayer.stage = MpStage::Caves;
    rig.front.cursor = { 400.0f, 300.0f };
    rig.Frame();
    Check(rig.model.TabPrevHighlighted(), "the cursor on the previous tab highlights it");
    Check(rig.Frame(Press(Button::kA)), "pressing it leaves");
    Check(rig.front.tabPrevSelected && !rig.front.tabNextSelected, "by the previous tab");
    Check(rig.front.multiplayer.stage == MpStage::Caves, "with the stage unchanged");
    Check(rig.model.Sfx().size() == 1, "and a sound");

    Rig back(2);
    back.front.multiplayer.stage = MpStage::Caves;
    back.CursorOn(1, 1);
    back.Frame();
    Check(back.Frame(Press(Button::kB)), "B leaves");
    Check(back.front.multiplayer.stage == MpStage::Caves && back.front.tabPrevSelected, "without choosing");
    Check(back.model.Sfx().size() == 1 && back.model.Sfx()[0] == FolderSfx::DoorMetalClose2, "and the door sounds");
}

void TestNothingHappensWithoutAPress() {
    Rig rig(2);
    rig.CursorOn(1, 1);
    Check(!rig.Frame(), "an idle frame stays");
    Check(rig.model.Sfx().empty(), "silently");
}

void TestStickMovesTheCursor() {
    Rig rig(2);
    rig.front.cursor = { 220.0f, 165.0f };
    const float before = rig.front.cursor.h;
    rig.Frame({ .stickX = 60 });
    Check(rig.front.cursor.h > before, "the stick moves the cursor right");
}

void TestPreviewsMatchTheStageTable() {
    // Library, Basement and Stack share the basement's image.
    Check(MpStageSelectModel::kPhoto[static_cast<int>(MpStage::Library)] == 13 &&
              MpStageSelectModel::kPhoto[static_cast<int>(MpStage::Basement)] == 13 &&
              MpStageSelectModel::kPhoto[static_cast<int>(MpStage::Stack)] == 13,
          "three stages share one preview");
    Check(MpStageSelectModel::kPhoto[static_cast<int>(MpStage::Random)] == 16, "Random has the last preview");
    Check(MpStageSelectModel::kPhoto[static_cast<int>(MpStage::Bunker)] == 0, "Bunker has the first");
    for (const int photo : MpStageSelectModel::kPhoto) {
        Check(photo >= 0 && photo < 17, "every preview is one of the seventeen");
    }
}

// The film strips are black the whole way across and their previews sit inside
// them, with the dots along each strip's edges and the captions over the foot of
// the previews, in the strip.
void TestFilmStripsHoldTheirPreviews() {
    using namespace Frigate::Game::Menu::MpStageSelectLayout;
    Check(kRuleBottom - kRuleTop == 52, "a strip's black rule is 52 rows high");
    for (int row = 0; row < MpStageSelectModel::kRows; row++) {
        const auto offset = static_cast<float>(row) * kPreviewRowSpacing;
        const float top = kPreviewFirstY + offset - kPreviewHalfHeight;
        const float bottom = kPreviewFirstY + offset + kPreviewHalfHeight;
        const auto ruleTop = static_cast<float>(kRuleTop + (row * kRuleSpacing));
        const auto ruleBottom = static_cast<float>(kRuleBottom + (row * kRuleSpacing));
        Check(top >= ruleTop && bottom <= ruleBottom, "a row of previews is inside its strip");
        Check(kDotFirstY + offset + kDotHalfHeight == ruleTop, "the dots run along the top of the strip");
        Check(kDotFirstY + offset + kDotPairSpacing - kDotHalfHeight == ruleBottom,
              "and along the bottom, so the strip is black between them");
        const auto captionBottom = static_cast<float>(kCaptionFirstBottom + (row * kCaptionRowSpacing));
        Check(captionBottom > top && captionBottom < ruleBottom, "a caption is drawn over the foot of its preview");
        for (int column = 0; column < MpStageSelectModel::kColumns; column++) {
            const float left =
                kPreviewFirstX + (static_cast<float>(column) * kPreviewColumnSpacing) - kPreviewHalfWidth;
            Check(left >= static_cast<float>(kRuleLeft) &&
                      left + (kPreviewHalfWidth * 2.0f) <= static_cast<float>(kRuleRight),
                  "every preview is between the ends of the strip");
        }
    }
    Check(kPreviewRowSpacing == static_cast<float>(kRuleSpacing), "a row of previews is a strip further down");
}

// Which previews are dimmed: the highlighted one is bright, an open one dim and
// a locked one nearly black, and a stage is locked while the folder has not
// opened it and when it holds fewer players than the game has.
void TestLockedPreviewsAreDimmedByProgressAndPlayers() {
    using namespace Frigate::Game::Menu::MpStageSelectLayout;
    Check(PreviewTint(true, true) == 0xFF && PreviewTint(true, false) == 0xFF, "the highlighted preview is bright");
    Check(PreviewTint(false, true) == 0x6E && PreviewTint(false, false) == 0x0F,
          "an open one is dim, a locked one dark");

    const auto open = [](int players, bool everyStage) {
        Rig rig(players, everyStage);
        const int count = static_cast<int>(MpStage::Count);
        std::array<bool, kMpStageCount> unlocked = MultiplayerUnlocks::UnlockedStages(*rig.front.progress, players);
        int open = 0;
        for (int stage = 0; stage < count; stage++) {
            open += unlocked[static_cast<size_t>(stage)] ? 1 : 0;
        }
        return std::pair(open, unlocked);
    };
    // A fresh folder opens Random and the first six stages whatever the players.
    for (int players = 2; players <= 4; players++) {
        const auto [fresh, unlocked] = open(players, false);
        Check(fresh == 7, "a fresh folder has the first seven previews lit");
        Check(!unlocked[static_cast<size_t>(MpStage::Facility)], "and Facility dark");
    }
    // With every stage open it is the player count that darkens the rest: Bunker,
    // Archives and Caverns take three, Egypt two.
    const auto [two, twoOpen] = open(2, true);
    const auto [three, threeOpen] = open(3, true);
    const auto [four, fourOpen] = open(4, true);
    Check(two == 12 && three == 11 && four == 8,
          "twelve previews are lit for two players, eleven for three, eight for four");
    Check(twoOpen[static_cast<size_t>(MpStage::Egypt)] && !threeOpen[static_cast<size_t>(MpStage::Egypt)],
          "Egypt takes two players");
    Check(threeOpen[static_cast<size_t>(MpStage::Caverns)] && !fourOpen[static_cast<size_t>(MpStage::Caverns)],
          "Caverns, Archives and Bunker take three");
    Check(fourOpen[static_cast<size_t>(MpStage::Facility)], "Facility takes four");
    // So the whole third row is dark for four players.
    for (int column = 0; column < MpStageSelectModel::kColumns; column++) {
        Check(!fourOpen[static_cast<size_t>(8 + column)], "the third row is dark for four players");
    }
}

} // namespace

int main() {
    TestFilmStripsHoldTheirPreviews();
    TestLockedPreviewsAreDimmedByProgressAndPlayers();
    TestInitHighlightsRandomAndClearsTheTabs();
    TestTheCursorPicksTheCell();
    TestLockedStagesFallBackToTheEarlierOpenOne();
    TestAHeldButtonFreezesTheHighlight();
    TestChoosingStoresTheStageAndLeaves();
    TestRandomIsChosenAsRandom();
    TestChoosingALockedCellChoosesTheFallback();
    TestPreviousTabAndBackLeaveWithoutChoosing();
    TestNothingHappensWithoutAPress();
    TestStickMovesTheCursor();
    TestPreviewsMatchTheStageTable();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    return 0;
}
