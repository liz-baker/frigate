#include "game/menu/BriefingModel.h"

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/FolderTabs.h"
#include "game/menu/MissionFolders.h"
#include "game/menu/StageName.h"
#include "game/menu/StageStart.h"
#include "game/save/GameSettings.h"
#include "game/save/SaveProgress.h"
#include "level/Lang.h"

using namespace Frigate::Game;
using namespace Frigate::Game::Menu;
using Frigate::Level::Lang;
using Frigate::Level::StringId;
namespace Button = Frigate::Game::Input::Button;
namespace TextBank = Frigate::Level::TextBank;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

Input::Sample Press(uint16_t buttons) {
    Input::Sample sample;
    sample.pressed = buttons;
    sample.held = buttons;
    return sample;
}

// A cursor mid-screen, off every tab.
MenuCursor CursorOffTabs() {
    return { .h = 220.0f, .v = 165.0f };
}

MenuCursor CursorOn(float h, float v) {
    return { .h = h, .v = v };
}

constexpr int32_t kMissionNum = 7;

std::array<uint8_t, BriefingData::kByteSize> DamBriefing() {
    // brief: 0x2C00..0x2C03; objectives (0x2C04, Secret), (0x2C05, 00), (0x2C06, 00), (0x2C07, Agent).
    std::array<uint8_t, BriefingData::kByteSize> bytes{};
    const auto put = [&bytes](size_t offset, uint16_t value) {
        bytes[offset] = static_cast<uint8_t>(value >> 8);
        bytes[offset + 1] = static_cast<uint8_t>(value);
    };
    for (uint16_t i = 0; i < 4; i++) {
        put(i * 2, StringId(11, i));
    }
    const uint16_t difficulties[] = { 1, 2, 2, 0 };
    for (uint16_t i = 0; i < 4; i++) {
        put(8 + (i * 4), StringId(11, 4 + i));
        put(10 + (i * 4), difficulties[i]);
    }
    return bytes;
}

void TestParse() {
    const auto bytes = DamBriefing();
    const auto data = BriefingData::Parse(bytes);
    Check(data.has_value(), "a whole briefing file parses");
    Check(data->brief[0] == StringId(11, 0) && data->brief[3] == StringId(11, 3), "the four page text ids are read");
    Check(data->objectives[0].textId == StringId(11, 4) && data->objectives[0].difficulty == 1,
          "an objective has its text and lowest difficulty");
    Check(data->objectives[4].textId == 0, "an unassigned objective has no text");
    Check(!BriefingData::Parse(std::span<const uint8_t>(bytes.data(), bytes.size() - 1)), "a short file does not");

    Check(data->ObjectivesAt(0) == std::vector<uint16_t>{ StringId(11, 7) }, "Agent shows only the Agent objective");
    Check(data->ObjectivesAt(1) == (std::vector<uint16_t>{ StringId(11, 4), StringId(11, 7) }),
          "Secret Agent adds the Secret Agent one, in file order");
    Check(data->ObjectivesAt(2).size() == 4, "00 Agent shows all four");
    Check(data->ObjectivesAt(3).size() == 4, "and so does 007");
}

void TestInit() {
    BriefingModel model;
    MenuCursor cursor = CursorOffTabs();
    model.Update(Press(Button::kZ), cursor, kMissionNum);
    model.Init();
    Check(model.Page() == BriefingPage::Title, "init goes to the first page");
    Check(!model.TabNextSelected() && !model.TabPrevSelected(), "and clears the selected tabs");
}

void TestPageProgression() {
    BriefingModel model;
    model.Init();
    MenuCursor cursor = CursorOffTabs();

    // Off the tabs, the NEXT tab is highlighted while there is a next page.
    BriefingModel::Next next = model.Update({}, cursor, kMissionNum);
    Check(next == BriefingModel::Next::Stay && model.Tabs().next && !model.Tabs().start && !model.Tabs().previous,
          "NEXT is highlighted by default");

    for (int page = 1; page <= 4; page++) {
        next = model.Update(Press(Button::kA), cursor, kMissionNum);
        Check(next == BriefingModel::Next::Stay && static_cast<int>(model.Page()) == page,
              "A on NEXT turns to the next page");
        Check(model.Sfx().size() == 1 && model.Sfx()[0] == FolderSfx::DoorMetalClose2, "with the click");
    }
    Check(model.Page() == BriefingPage::Moneypenny && !model.ShowsNextTab(), "the last page has no NEXT tab");

    model.Update({}, cursor, kMissionNum);
    Check(model.Tabs().start && !model.Tabs().next, "START is highlighted by default on the last page");

    next = model.Update(Press(Button::kZ), cursor, kMissionNum);
    Check(next == BriefingModel::Next::RunStage && model.TabNextSelected(), "Z on START runs the stage");
}

void TestNextTabOnLastPageHighlightsStart() {
    BriefingModel model;
    model.Init();
    MenuCursor cursor = CursorOffTabs();
    for (int i = 0; i < 4; i++) {
        model.Update(Press(Button::kA), cursor, kMissionNum);
    }
    cursor = CursorOn(400.0f, 150.0f);
    model.Update({}, cursor, kMissionNum);
    Check(model.Tabs().start && !model.Tabs().next, "the cursor on NEXT of the last page falls through to START");
}

void TestBackStepsThenLeaves() {
    BriefingModel model;
    model.Init();
    MenuCursor cursor = CursorOffTabs();
    model.Update(Press(Button::kA), cursor, kMissionNum);
    model.Update(Press(Button::kA), cursor, kMissionNum);
    Check(model.Page() == BriefingPage::M, "two pages in");

    BriefingModel::Next next = model.Update(Press(Button::kB), cursor, kMissionNum);
    Check(next == BriefingModel::Next::Stay && model.Page() == BriefingPage::Overview, "B turns back a page");
    next = model.Update(Press(Button::kB), cursor, kMissionNum);
    Check(next == BriefingModel::Next::Stay && model.Page() == BriefingPage::Title, "and again");

    next = model.Update(Press(Button::kB), cursor, kMissionNum);
    Check(next == BriefingModel::Next::MissionSelect && model.TabPrevSelected(), "B on the first page leaves");
    Check(model.Sfx().size() == 1, "with the click");
    // Mission 7 is the third column of the second row.
    Check(cursor.h == 212.0f && cursor.v == 131.0f, "and puts the mission select's cursor on the stage");
}

void TestPreviousTab() {
    BriefingModel model;
    model.Init();
    MenuCursor cursor = CursorOn(400.0f, 260.0f);
    model.Update({}, cursor, kMissionNum);
    Check(model.Tabs().previous && !model.Tabs().next, "the cursor on PREVIOUS highlights it");

    BriefingModel again;
    again.Init();
    cursor = CursorOn(400.0f, 260.0f);
    BriefingModel::Next next = again.Update(Press(Button::kA), cursor, kMissionNum);
    Check(next == BriefingModel::Next::MissionSelect, "A on PREVIOUS of the first page goes back");

    BriefingModel later;
    later.Init();
    MenuCursor off = CursorOffTabs();
    later.Update(Press(Button::kA), off, kMissionNum);
    cursor = CursorOn(400.0f, 260.0f);
    next = later.Update(Press(Button::kA), cursor, kMissionNum);
    Check(next == BriefingModel::Next::Stay && later.Page() == BriefingPage::Title,
          "A on PREVIOUS of a later page turns back one");
}

void TestStartTabAndButton() {
    BriefingModel model;
    model.Init();
    MenuCursor cursor = CursorOn(400.0f, 80.0f);
    model.Update({}, cursor, kMissionNum);
    Check(model.Tabs().start && !model.Tabs().next, "the cursor on START highlights it");
    BriefingModel::Next next = model.Update(Press(Button::kA), cursor, kMissionNum);
    Check(next == BriefingModel::Next::RunStage, "A on START runs the stage from the first page");

    BriefingModel pressed;
    pressed.Init();
    cursor = CursorOffTabs();
    next = pressed.Update(Press(Button::kStart), cursor, kMissionNum);
    Check(next == BriefingModel::Next::RunStage && pressed.Page() == BriefingPage::Title,
          "the Start button runs the stage from any page");
    Check(pressed.Sfx().size() == 1, "with the click");
}

void TestCursorMovesWithStick() {
    BriefingModel model;
    model.Init();
    MenuCursor cursor = CursorOffTabs();
    Input::Sample sample;
    sample.stickX = 40;
    model.Update(sample, cursor, kMissionNum);
    Check(cursor.h > 220.0f, "the stick moves the cursor");
}

void TestTabHitAreas() {
    Check(CursorOnStartTab(CursorOn(391.0f, 130.5f)) && !CursorOnStartTab(CursorOn(391.0f, 131.0f)),
          "START covers down to 130.5");
    Check(!CursorOnStartTab(CursorOn(390.0f, 50.0f)), "and is right of 390");
    Check(CursorOnNextTab(CursorOn(400.0f, 223.0f)) && !CursorOnNextTab(CursorOn(400.0f, 130.5f)),
          "NEXT covers past 130.5 down to 223");
    Check(CursorOnPreviousTab(CursorOn(400.0f, 223.5f)) && !CursorOnPreviousTab(CursorOn(400.0f, 223.0f)),
          "PREVIOUS is below 223");
    MenuCursor cursor;
    SetCursorToNextTab(cursor);
    Check(cursor.h == 399.0f && cursor.v == 144.0f, "the cursor goes to the NEXT tab");
}

Lang TitleAndDamLang() {
    Lang lang;
    std::vector<std::string> title(130);
    title[19] = "Agent";
    title[20] = "Secret Agent";
    title[21] = "00 Agent";
    title[22] = "007";
    title[32] = " JB\n";
    title[33] = "Mission ";
    title[34] = "Part ";
    title[93] = "Primary Objectives\n";
    title[94] = "Background\n";
    title[95] = "M's Briefing\n";
    title[96] = "Q Branch\n";
    title[97] = "Moneypenny\n";
    title[120] = "Arkangel";
    title[121] = "Dam";
    lang.SetBank(TextBank::Title, title);

    std::vector<std::string> dam(8);
    dam[0] = "Background text\n";
    dam[1] = "M text\n";
    dam[2] = "Q text\n";
    dam[3] = "Moneypenny text\n";
    lang.SetBank(11, dam);
    return lang;
}

void TestText() {
    const Lang lang = TitleAndDamLang();
    // Row 1 of the mission folders: Dam, "i" under chapter "1".
    const auto title = [&lang](size_t slot) {
        return std::string(lang.Get(StringId(TextBank::Title, static_cast<int32_t>(slot))));
    };
    const StageName name = BuildStageName(title, 2, 1);
    Check(name.difficulty == "00 Agent JB\n", "the difficulty's name and the JB suffix");
    Check(name.chapter == "Mission 1: Arkangel\n", "the chapter, from the heading row above");
    Check(name.part == "Part i: Dam\n", "and the part");

    const StageName multi = BuildStageName(title, -1, 1);
    Check(multi.difficulty.empty(), "no difficulty is shown without one selected");

    Check(PageHeading(lang, BriefingPage::Title) == "Primary Objectives\n", "the objectives heading");
    Check(PageHeading(lang, BriefingPage::Moneypenny) == "Moneypenny\n", "the last page's heading");

    const auto bytes = DamBriefing();
    const auto data = BriefingData::Parse(bytes);
    Check(PageBody(lang, *data, BriefingPage::Overview) == "Background text\n", "the overview takes the first text id");
    Check(PageBody(lang, *data, BriefingPage::M) == "M text\n", "M the second");
    Check(PageBody(lang, *data, BriefingPage::Q) == "Q text\n", "Q the third");
    Check(PageBody(lang, *data, BriefingPage::Moneypenny) == "Moneypenny text\n", "Moneypenny the fourth");
    Check(PageBody(lang, *data, BriefingPage::Title).empty(), "the objectives page has no body");
    Check(lang.Get(data->objectives[0].textId).empty(), "an objective with no string reads as empty");

    for (size_t row = 0; row < MissionFolderEntries().size(); row++) {
        const MissionFolderSetup& entry = MissionFolderEntries()[row];
        if (entry.briefing != nullptr) {
            Check(Lang::StageBank(entry.stage) > 0, "every briefed stage has a text bank");
        }
    }
}

void TestStageStart() {
    auto storage = std::make_shared<Save::MemorySaveStorage>();
    auto manager = std::make_shared<Save::SaveManager>(storage);
    manager->ValidateSaves();

    FrontState front;
    front.save = manager;
    front.progress = std::make_shared<Save::SaveProgress>(manager);
    front.settings = std::make_shared<Save::GameSettings>();
    front.progress->SetSettingsCapture(
        [settings = front.settings](Save::SaveData& save) { Save::CaptureSettings(*settings, save); });

    Check(!StartSelectedStage(front), "with no stage selected there is nothing to start");

    // The options a folder was last left with come back when its stage starts.
    front.progress->SetCurrentFolder(1);
    front.settings->sfxVolume = 0x2000;
    front.settings->options.lookInverted = true;
    front.progress->DeleteCurrentSelectedFolder();
    *front.settings = Save::GameSettings();

    front.selectedStage = 4;
    front.progress->SetSelectedDifficulty(Save::kDifficulty00);
    const auto selected = StartSelectedStage(front);
    Check(selected && selected->stage == 4 && selected->difficulty == Save::kDifficulty00,
          "the selected stage and difficulty are handed over");
    Check(front.settings->sfxVolume == 0x2020 && front.settings->options.lookInverted,
          "with the folder's settings applied");

    // The watch's close saves them back into the folder.
    front.settings->options.lookInverted = false;
    SettingsSaver(front)();
    Save::GameSettings reloaded;
    front.progress->LoadSaveSettingsForSelectedFolder(
        [&reloaded](const Save::SaveData& save) { Save::ApplySettings(save, 1, reloaded); });
    Check(!reloaded.options.lookInverted, "the saver stores the changed settings in the folder");
}

} // namespace

int main() {
    TestParse();
    TestInit();
    TestPageProgression();
    TestNextTabOnLastPageHighlightsStart();
    TestBackStepsThenLeaves();
    TestPreviousTab();
    TestStartTabAndButton();
    TestCursorMovesWithStick();
    TestTabHitAreas();
    TestText();
    TestStageStart();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    return 0;
}
