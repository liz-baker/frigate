#include "game/menu/MissionEndModel.h"
#include "game/menu/TitleStrings.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "game/Input.h"
#include "game/save/SaveData.h"
#include "level/LevelInfo.h"

using namespace Frigate::Game;
using namespace Frigate::Game::Menu;
using Frigate::Level::ObjectiveStatus;
namespace Button = Frigate::Game::Input::Button;
namespace TS = MissionEnd::ReportString;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

void CheckEq(const std::string& actual, const std::string& expected, const char* what) {
    if (actual != expected) {
        std::fprintf(stderr, "FAIL: %s: got '%s', expected '%s'\n", what, actual.c_str(), expected.c_str());
        gFailures++;
    }
}

// Mission, difficulty and the time a folder holds.
struct FakeSave : MissionEndSave {
    struct Unlocked {
        int32_t folder;
        int32_t mission;
        int32_t difficulty;
        int32_t seconds;
    };
    uint32_t best = 0;
    std::vector<Unlocked> stages;
    std::vector<int32_t> cheats;
    std::vector<int32_t> cheatsOwned;

    uint32_t BestTime(int32_t, int32_t, int32_t) const override {
        return best;
    }
    void UnlockStage(int32_t folder, int32_t mission, int32_t difficulty, int32_t seconds) override {
        stages.push_back({ folder, mission, difficulty, seconds });
    }
    bool IsCheatUnlocked(int32_t, int32_t cheat) const override {
        return std::find(cheatsOwned.begin(), cheatsOwned.end(), cheat) != cheatsOwned.end();
    }
    void UnlockCheat(int32_t, int32_t cheat) override {
        cheats.push_back(cheat);
    }
};

// Strings by slot, with a 6 pixel advance and 10 pixel line.
struct FakeText : MissionEndText {
    std::map<int32_t, std::string> strings;

    FakeText() {
        const auto title = [this](int32_t slot, const char* text) { strings[TitleId(slot)] = text; };
        title(TitleString::kFirstDifficultyName + 2, "00 Agent");
        title(TitleString::kJamesBond, " JB");
        title(TitleString::kMission, "Mission ");
        title(TitleString::kPart, "Part ");
        title(91, "COMPLETED");
        title(92, "FAILED");
        title(TS::kReport, "REPORT:");
        title(TS::kMissionStatus, "Mission status:");
        title(TS::kKia, "KILLED IN ACTION");
        title(TS::kAborted, "ABORTED");
        title(TS::kCompleted, "Completed");
        title(TS::kFailed, "Mission failed");
        title(TS::kStatistics, "STATISTICS:\n");
        title(TS::kTime, "Time:");
        title(TS::kAccuracy, "Accuracy:");
        title(TS::kWeaponOfChoice, "Weapon of choice:");
        title(TS::kShotTotal, "Shot total:");
        title(TS::kHeadHits, "Head hits:");
        title(TS::kBodyHits, "Body hits:");
        title(TS::kLimbHits, "Limb hits:");
        title(TS::kOther, "Others:");
        title(TS::kKillTotal, "Kill total:");
        title(TS::kBestTime, "Best Time:");
        title(TS::kTarget, "Target:");
        title(TS::kNewCheatAvailable, "New Cheat Available");
        // The first mission's chapter and part names.
        title(120, "Arkangelsk");
        title(121, "Dam");
        strings[100] = "Neutralize alarms\n";
        strings[101] = "Install modem\n";
    }

    std::string Get(int32_t id) const override {
        const auto found = strings.find(id);
        return found == strings.end() ? std::string() : found->second;
    }
    Size Measure(std::string_view text) const override {
        Size size;
        int32_t line = 0;
        for (const char c : text) {
            if (c == '\n') {
                size.width = std::max(size.width, line);
                line = 0;
                size.height += 10;
            } else {
                line += 6;
            }
        }
        size.width = std::max(size.width, line);
        return size;
    }
    std::string Wrap(int32_t, std::string_view text, int32_t) const override {
        return std::string(text);
    }
    std::string WeaponOfChoiceName(int32_t item) const override {
        return item == 2 ? "PP7\n" : "";
    }
};

// Dam at 00 Agent in the first folder, with two objectives of which the second
// only applies from Secret Agent.
MissionContext DamContext() {
    MissionContext context;
    context.briefingPage = 1;
    context.missionNum = Save::kLevelDam;
    context.folder = 0;
    context.difficulty = Save::kDifficulty00;
    context.objectives[0] = { 100, Save::kDifficultyAgent };
    context.objectives[1] = { 101, Save::kDifficultySecret };
    return context;
}

MissionOutcome CompletedOutcome() {
    MissionOutcome outcome;
    outcome.objectives[0] = ObjectiveStatus::Complete;
    outcome.objectives[1] = ObjectiveStatus::Complete;
    return outcome;
}

Input::Sample Press(uint16_t buttons) {
    Input::Sample sample;
    sample.pressed = buttons;
    sample.held = buttons;
    return sample;
}

// A line by its text, or null.
const TextLine* Find(const std::vector<TextLine>& lines, const std::string& text) {
    for (const TextLine& line : lines) {
        if (line.text == text) {
            return &line;
        }
    }
    return nullptr;
}

void TestObjectiveChecks() {
    const MissionContext context = DamContext();
    MissionOutcome outcome = CompletedOutcome();
    Check(MissionEnd::AllObjectivesAliveSuccess(outcome, context), "all complete is a success");
    Check(MissionEnd::AllObjectivesComplete(outcome, context), "all complete");
    Check(MissionEnd::StatusOf(outcome, context) == MissionStatus::Completed, "completed status");

    outcome.objectives[1] = ObjectiveStatus::Failed;
    Check(!MissionEnd::AllObjectivesAliveSuccess(outcome, context), "a failed objective is no success");
    Check(!MissionEnd::AllObjectivesComplete(outcome, context), "a failed objective is not complete");
    Check(MissionEnd::StatusOf(outcome, context) == MissionStatus::Failed, "failed status");

    // An objective above the difficulty does not count.
    MissionContext agent = context;
    agent.difficulty = Save::kDifficultyAgent;
    agent.objectives[1].enabledDifficulty = Save::kDifficultySecret;
    Check(MissionEnd::AllObjectivesAliveSuccess(outcome, agent), "objectives above the difficulty are ignored");

    outcome = CompletedOutcome();
    outcome.bondKilledInAction = true;
    Check(!MissionEnd::AllObjectivesAliveSuccess(outcome, context), "KIA is no success");
    Check(MissionEnd::AllObjectivesComplete(outcome, context), "KIA leaves the objectives as they were");
    Check(MissionEnd::StatusOf(outcome, context) == MissionStatus::KilledInAction, "KIA status");

    outcome.bondKilledInAction = false;
    outcome.missionFailedOrAborted = true;
    Check(MissionEnd::StatusOf(outcome, context) == MissionStatus::Aborted, "aborted status");

    // KIA outranks an abort.
    outcome.bondKilledInAction = true;
    Check(MissionEnd::StatusOf(outcome, context) == MissionStatus::KilledInAction, "KIA over aborted");

    // An unassigned slot is never required.
    MissionOutcome empty;
    MissionContext none;
    Check(MissionEnd::AllObjectivesAliveSuccess(empty, none), "no objectives is a success");
}

void TestTargetTimes() {
    Check(MissionEnd::TargetTime(Save::kLevelDam, Save::kDifficultySecret) == 160, "Dam at Secret Agent");
    Check(MissionEnd::TargetTime(Save::kLevelDam, Save::kDifficultyAgent) == 0, "Dam has no Agent par");
    Check(MissionEnd::TargetTime(Save::kLevelFacility, Save::kDifficulty00) == 125, "Facility at 00 Agent");
    Check(MissionEnd::TargetTime(Save::kLevelCradle, Save::kDifficultyAgent) == 135, "Cradle at Agent");
    Check(MissionEnd::TargetTime(Save::kLevelEgypt, Save::kDifficulty00) == 360, "Egypt at 00 Agent");
    Check(MissionEnd::TargetTime(Save::kLevelDam, Save::kDifficulty007) == 0, "007 has no par");
    Check(MissionEnd::TargetTime(20, 0) == 0, "past the table");
}

void TestEndOfMissionRecording() {
    // Dam at Secret Agent, par 160 seconds.
    MissionContext context = DamContext();
    context.difficulty = Save::kDifficultySecret;
    context.folder = 2;

    MissionOutcome outcome = CompletedOutcome();
    outcome.missionTimer = 150 * 60 + 59;
    FakeSave save;
    bool cheat = MissionEnd::EndOfMissionBriefing(save, outcome, context, false);
    Check(save.stages.size() == 1, "the stage is recorded");
    Check(save.stages[0].folder == 2 && save.stages[0].mission == Save::kLevelDam &&
              save.stages[0].difficulty == Save::kDifficultySecret && save.stages[0].seconds == 150,
          "the folder, mission, difficulty and whole seconds are recorded");
    Check(cheat && save.cheats.size() == 1 && save.cheats[0] == Save::kLevelDam, "beating par unlocks the cheat");

    // Exactly the par time counts.
    save = FakeSave{};
    outcome.missionTimer = 160 * 60;
    cheat = MissionEnd::EndOfMissionBriefing(save, outcome, context, false);
    Check(cheat && save.cheats.size() == 1, "the par time itself unlocks the cheat");

    // One second over.
    save = FakeSave{};
    outcome.missionTimer = 161 * 60;
    cheat = MissionEnd::EndOfMissionBriefing(save, outcome, context, true);
    Check(!cheat && save.cheats.empty() && save.stages.size() == 1, "over par records the time but no cheat");

    // Already unlocked: nothing new to announce.
    save = FakeSave{};
    save.cheatsOwned.push_back(Save::kLevelDam);
    outcome.missionTimer = 100 * 60;
    cheat = MissionEnd::EndOfMissionBriefing(save, outcome, context, true);
    Check(!cheat && save.cheats.empty(), "an owned cheat is not unlocked again");

    // Difficulties with no par for the mission never unlock (par 0).
    save = FakeSave{};
    context.difficulty = Save::kDifficultyAgent;
    outcome.missionTimer = 100 * 60;
    cheat = MissionEnd::EndOfMissionBriefing(save, outcome, context, false);
    Check(!cheat && save.cheats.empty() && save.stages.size() == 1, "no par at the difficulty, no cheat");

    // Under a minute beats a par of 0: the comparison is on whole seconds.
    save = FakeSave{};
    outcome.missionTimer = 0;
    cheat = MissionEnd::EndOfMissionBriefing(save, outcome, context, false);
    Check(cheat && save.cheats.size() == 1, "0 seconds is within a par of 0");

    // Nothing is recorded in 007 mode, with cheats on, or without a page.
    save = FakeSave{};
    context.difficulty = Save::kDifficulty007;
    Check(MissionEnd::EndOfMissionBriefing(save, outcome, context, true), "007 leaves the notice as it was");
    Check(save.stages.empty(), "007 records nothing");

    context.difficulty = Save::kDifficultySecret;
    context.cheatsAppended = true;
    Check(!MissionEnd::EndOfMissionBriefing(save, outcome, context, false), "cheats on leave the notice");
    Check(save.stages.empty(), "cheats on record nothing");

    context.cheatsAppended = false;
    context.briefingPage = -1;
    MissionEnd::EndOfMissionBriefing(save, outcome, context, false);
    Check(save.stages.empty(), "no briefing page records nothing");
}

void TestStageName() {
    FakeText text;
    const auto lines = MissionEnd::StageNameLines(text, DamContext());
    Check(lines.size() == 3, "difficulty, chapter and part lines");
    CheckEq(lines[0].text, "00 Agent JB", "the difficulty line");
    Check(lines[0].x == 0x37 && lines[0].y == 0x57, "the difficulty position");
    CheckEq(lines[1].text, "Mission 1: Arkangelsk\n", "the chapter line");
    Check(lines[1].y == 0x67, "the chapter row");
    CheckEq(lines[2].text, "Part i: Dam\n", "the part line");
    Check(lines[2].y == 0x77, "the part row");

    MissionContext multi = DamContext();
    multi.difficulty = Save::kDifficultyMulti;
    multi.briefingPage = -1;
    Check(MissionEnd::StageNameLines(text, multi).empty(), "no selection, no lines");
}

void TestFailedPage() {
    FakeText text;
    MissionContext context = DamContext();
    MissionOutcome outcome = CompletedOutcome();

    auto lines = MissionEnd::MissionFailedLines(text, outcome, context);
    const TextLine* report = Find(lines, "REPORT:");
    Check(report && report->x == 0x37 && report->y == 0x8F, "the report heading");
    const TextLine* status = Find(lines, "Completed");
    Check(status != nullptr, "the completed verdict");
    // Beside the heading: its width (15 characters) and the margin.
    Check(status && status->x == (15 * 6) + 0x37 && status->y == 0xA7, "the verdict follows the heading");
    Check(status && status->colour == PageColour::kBlack, "a completed verdict is black");

    // Both objectives are listed, lettered, with their verdicts.
    const TextLine* first = Find(lines, "a.\n");
    const TextLine* second = Find(lines, "b.\n");
    Check(first && first->x == 0x37 && first->y == 0xBF, "the first objective's letter");
    Check(second && second->y == 0xBF + 10, "the second objective is one line down");
    const TextLine* text1 = Find(lines, "Neutralize alarms\n");
    Check(text1 && text1->x == 0x4B && text1->y == 0xBF, "the objective text");
    Check(Find(lines, "COMPLETED") != nullptr && Find(lines, "FAILED") == nullptr, "complete objectives");

    outcome.objectives[1] = ObjectiveStatus::Failed;
    lines = MissionEnd::MissionFailedLines(text, outcome, context);
    const TextLine* failed = Find(lines, "FAILED");
    Check(failed && failed->x == 0x136 && failed->colour == PageColour::kFailedRed, "a failed objective is red");
    Check(Find(lines, "Completed") == nullptr, "the verdict is no longer completed");
    const TextLine* verdict = Find(lines, "Mission failed");
    Check(verdict && verdict->colour == PageColour::kFailedRed, "the failed verdict is red");

    outcome.bondKilledInAction = true;
    lines = MissionEnd::MissionFailedLines(text, outcome, context);
    const TextLine* kia = Find(lines, "KILLED IN ACTION");
    Check(kia && kia->colour == PageColour::kFailedRed, "KIA verdict");

    outcome.bondKilledInAction = false;
    outcome.missionFailedOrAborted = true;
    lines = MissionEnd::MissionFailedLines(text, outcome, context);
    Check(Find(lines, "ABORTED") != nullptr, "aborted verdict");

    // An objective above the difficulty is not listed.
    context.difficulty = Save::kDifficultyAgent;
    lines = MissionEnd::MissionFailedLines(text, outcome, context);
    Check(Find(lines, "b.\n") == nullptr && Find(lines, "a.\n") != nullptr, "objectives above the difficulty");
}

void TestCompletePage() {
    FakeText text;
    MissionContext context = DamContext();
    context.difficulty = Save::kDifficultySecret;
    MissionOutcome outcome = CompletedOutcome();
    outcome.missionTimer = ((2 * 60) + 14) * 60 + 30;
    outcome.killCount = 17;
    outcome.shots = { 120, 6, 18, 4, 1, 0, 2 };
    outcome.favouriteWeapon = { 2, 2 };

    auto lines = MissionEnd::MissionCompleteLines(text, outcome, context, 0, false);
    const TextLine* time = Find(lines, "02:14");
    Check(time && time->x == 0x82 && time->y == 0xA7, "the mission time");

    // Accuracy: 31 hits (objects included) of 120 shots.
    const TextLine* accuracy = Find(lines, "Accuracy:");
    Check(accuracy && accuracy->y == 0xCC, "the accuracy row");
    Check(Find(lines, "25.8%") != nullptr, "accuracy percentage");
    Check(Find(lines, "120") != nullptr && Find(lines, "17") != nullptr, "shot and kill totals");

    // Head 6, body 18, limb 4, others 1 of 29 hits.
    Check(Find(lines, "6 (21%)") != nullptr, "head hits");
    Check(Find(lines, "18 (62%)") != nullptr, "body hits");
    Check(Find(lines, "4 (14%)") != nullptr, "limb hits");
    Check(Find(lines, "1 (3%)") != nullptr, "other hits");

    // Both hands carrying the same weapon.
    Check(Find(lines, "PP7 x 2\n") != nullptr, "the weapon in both hands");

    // The row pitch is the statistics heading's height (10).
    const TextLine* body = Find(lines, "Body hits:");
    Check(body && body->y == 0xF4 + 10, "the body hits row");
    const TextLine* other = Find(lines, "Others:");
    Check(other && other->y == 0xF4 + 30, "the others row");
    const TextLine* kills = Find(lines, "Kill total:");
    Check(kills && kills->y == 0xF4 + 10, "the kill total row");

    // Target for Dam at Secret Agent is 2:40; no best time stored yet.
    Check(Find(lines, "02:40") != nullptr && Find(lines, "Target:") != nullptr, "the target time");
    Check(Find(lines, "     (Best Time:  00:00)") == nullptr, "no best time before one is stored");
    Check(Find(lines, "Best Time:") == nullptr, "no best time label with a target");

    // A stored best time follows the target, in brackets.
    lines = MissionEnd::MissionCompleteLines(text, outcome, context, 125, false);
    const TextLine* best = Find(lines, "     (Best Time:  02:05)");
    const TextLine* target = Find(lines, "02:40");
    Check(best && target && best->x == 0x82 + (5 * 6) && best->y == target->y, "the best time beside the target");

    // 007's marker and an unset time are no best time.
    lines = MissionEnd::MissionCompleteLines(text, outcome, context, 0x3FF, false);
    Check(Find(lines, "     (Best Time:  17:03)") == nullptr, "0x3FF is not a time");

    // Without a par (Agent on Dam) the best time stands alone.
    context.difficulty = Save::kDifficultyAgent;
    lines = MissionEnd::MissionCompleteLines(text, outcome, context, 125, false);
    Check(Find(lines, "Target:") == nullptr, "no target without a par");
    const TextLine* label = Find(lines, "Best Time:");
    const TextLine* value = Find(lines, "02:05");
    Check(label && value && label->x == 0x37 && value->x == 0x82, "the best time on its own");

    // 007 mode shows no target, and a par of its own is looked up as 00 Agent's.
    context.difficulty = Save::kDifficulty007;
    lines = MissionEnd::MissionCompleteLines(text, outcome, context, 0, false);
    Check(Find(lines, "Target:") == nullptr, "no target in 007 mode");

    // The new cheat notice follows the time.
    lines = MissionEnd::MissionCompleteLines(text, outcome, context, 0, true);
    const TextLine* notice = Find(lines, "     [New Cheat Available]");
    Check(notice && notice->colour == MissionEnd::kNewCheatRed && notice->x == 0x82 + 30 && notice->y == 0xA7,
          "the new cheat notice");
    Check(Find(MissionEnd::MissionCompleteLines(text, outcome, context, 0, false), "     [New Cheat Available]") ==
              nullptr,
          "no notice without a new cheat");

    // No shots fired reads as 0.0%, and a lone weapon has no multiplier.
    MissionOutcome idle = CompletedOutcome();
    idle.favouriteWeapon = { 2, 0 };
    lines = MissionEnd::MissionCompleteLines(text, idle, context, 0, false);
    Check(Find(lines, "0.0%") != nullptr, "no shots fired");
    Check(Find(lines, "PP7\n") != nullptr, "one weapon");
    Check(Find(lines, "0 (0%)") != nullptr, "no hits");
}

void TestFailedTabs() {
    const MissionContext context = DamContext();
    MenuCursor cursor;
    MissionFailedModel model;
    model.Init(cursor);
    Check(cursor.h == 399.0f && cursor.v == 144.0f, "the cursor starts on Next");

    // Idle frames stay put and highlight Next.
    Check(model.Update({}, cursor, context) == MissionFailedModel::Next::Stay, "idle");
    Check(model.Tabs().nextHighlight && !model.Tabs().prevHighlight, "Next is highlighted");

    // Z, A or Start on Next goes on to the statistics, with the door sound.
    for (const uint16_t button : { Button::kA, Button::kZ, Button::kStart }) {
        model.Init(cursor);
        Check(model.Update(Press(button), cursor, context) == MissionFailedModel::Next::MissionComplete,
              "a select button on Next");
        Check(model.Sfx().size() == 1, "the tab sounds");
    }

    // B goes back to the mission list with the cursor on the mission.
    model.Init(cursor);
    Check(model.Update(Press(Button::kB), cursor, context) == MissionFailedModel::Next::MissionSelect, "B");
    Check(cursor.h == 73.0f && cursor.v == 62.0f, "the cursor goes to Dam on the mission list");

    // A on Previous does the same.
    cursor.h = 400.0f;
    cursor.v = 250.0f;
    model.Init(cursor);
    cursor.h = 400.0f;
    cursor.v = 250.0f;
    Check(model.Update(Press(Button::kA), cursor, context) == MissionFailedModel::Next::MissionSelect, "A on Previous");
    Check(model.Tabs().prevHighlight && !model.Tabs().nextHighlight, "Previous is highlighted");

    // Other buttons do nothing.
    model.Init(cursor);
    Check(model.Update(Press(Button::kL), cursor, context) == MissionFailedModel::Next::Stay, "other buttons");
    Check(model.Sfx().empty(), "no sound");

    // The stick moves the cursor.
    model.Init(cursor);
    const float before = cursor.h;
    Input::Sample stick;
    stick.stickX = -60;
    model.Update(stick, cursor, context);
    Check(cursor.h < before, "the stick moves the cursor");
}

void TestCompleteTabs() {
    MenuCursor cursor;
    MissionFlow flow{ 1, static_cast<int32_t>(Frigate::Level::LevelId::Dam) };
    MissionContext context = DamContext();
    MissionOutcome outcome = CompletedOutcome();
    MissionCompleteModel model;
    Check(!model.Init(false) && model.Init(true), "the new cheat sound follows the notice");

    cursor = { 399.0f, 144.0f };
    // A clean completion carries on to the next part of the chapter.
    model.Init(false);
    Check(model.Update(Press(Button::kA), cursor, outcome, context, flow) == MissionCompleteModel::Next::Briefing,
          "Next after a completion");
    Check(flow.briefingPage == 2 && flow.selectedStage == static_cast<int32_t>(Frigate::Level::LevelId::Facility),
          "Facility follows Dam");

    // The last part of a chapter skips the next chapter's heading.
    context.briefingPage = 3;
    context.missionNum = 2;
    flow = { 3, static_cast<int32_t>(Frigate::Level::LevelId::Runway) };
    model.Init(false);
    cursor = { 399.0f, 144.0f };
    model.Update(Press(Button::kStart), cursor, outcome, context, flow);
    Check(flow.briefingPage == 5 && flow.selectedStage == static_cast<int32_t>(Frigate::Level::LevelId::Surface),
          "Surface follows Runway");

    // A failed objective retries: the same briefing.
    context.briefingPage = 1;
    context.missionNum = Save::kLevelDam;
    flow = { 1, static_cast<int32_t>(Frigate::Level::LevelId::Dam) };
    outcome.objectives[1] = ObjectiveStatus::Failed;
    model.Init(false);
    cursor = { 399.0f, 144.0f };
    Check(model.Update(Press(Button::kZ), cursor, outcome, context, flow) == MissionCompleteModel::Next::Briefing,
          "Next after a failure");
    Check(flow.briefingPage == 1 && flow.selectedStage == static_cast<int32_t>(Frigate::Level::LevelId::Dam),
          "a failure retries the same mission");

    // So do cheats, whatever the objectives.
    outcome = CompletedOutcome();
    context.cheatsAppended = true;
    model.Init(false);
    cursor = { 399.0f, 144.0f };
    model.Update(Press(Button::kZ), cursor, outcome, context, flow);
    Check(flow.briefingPage == 1, "cheats retry the same mission");
    context.cheatsAppended = false;

    // The Cradle goes on to the Cuba stage.
    context.briefingPage = 24;
    context.missionNum = Save::kLevelCradle;
    flow = { 24, static_cast<int32_t>(Frigate::Level::LevelId::Cradle) };
    model.Init(false);
    cursor = { 399.0f, 144.0f };
    Check(model.Update(Press(Button::kA), cursor, outcome, context, flow) == MissionCompleteModel::Next::RunStage,
          "the Cradle leads on");
    Check(flow.selectedStage == static_cast<int32_t>(Frigate::Level::LevelId::Cuba), "to Cuba");

    // The bonus missions go back to the list at their own place.
    context.briefingPage = 26;
    context.missionNum = Save::kLevelAztec;
    flow = { 26, static_cast<int32_t>(Frigate::Level::LevelId::Aztec) };
    model.Init(false);
    cursor = { 399.0f, 144.0f };
    Check(model.Update(Press(Button::kA), cursor, outcome, context, flow) == MissionCompleteModel::Next::MissionSelect,
          "Aztec returns to the list");
    Check(cursor.h == 282.0f && cursor.v == 270.0f, "at the Aztec slot");
    Check(flow.briefingPage == 26, "the page is unchanged");

    // Previous and B go back to the list from any mission.
    context.briefingPage = 1;
    context.missionNum = Save::kLevelDam;
    model.Init(false);
    cursor = { 399.0f, 144.0f };
    Check(model.Update(Press(Button::kB), cursor, outcome, context, flow) == MissionCompleteModel::Next::MissionSelect,
          "B");
    Check(cursor.h == 73.0f && cursor.v == 62.0f, "back at Dam");
    model.Init(false);
    cursor = { 400.0f, 260.0f };
    Check(model.Update(Press(Button::kA), cursor, outcome, context, flow) == MissionCompleteModel::Next::MissionSelect,
          "A on Previous");

    // Idle.
    model.Init(false);
    cursor = { 399.0f, 144.0f };
    Check(model.Update({}, cursor, outcome, context, flow) == MissionCompleteModel::Next::Stay, "idle");
}

void TestOutcomeOfFinishedStage() {
    Frigate::Level::StageResult result;
    result.exit = Frigate::Level::StageExit::ReturnTitle;
    result.missionTimer = 4321;
    result.killCount = 12;
    result.shots = { 80, 5, 20, 3, 1, 0, 2 };
    result.favouriteWeapon = { 3, 4 };
    result.objectives.fill(ObjectiveStatus::Complete);
    result.objectives[2] = ObjectiveStatus::Failed;
    result.objectivesCompleteAtExit = true;

    MissionOutcome outcome = MissionOutcomeOf(result);
    Check(outcome.missionTimer == 4321 && outcome.killCount == 12, "the timer and kill count carry over");
    Check(outcome.shots == result.shots, "and the shot registers");
    Check(outcome.favouriteWeapon[0] == 3 && outcome.favouriteWeapon[1] == 4, "and the weapon of choice");
    Check(outcome.objectives[2] == ObjectiveStatus::Failed && outcome.objectives[0] == ObjectiveStatus::Complete,
          "and each objective's status");
    Check(!outcome.bondKilledInAction && !outcome.missionFailedOrAborted, "alive and not aborted");
    Check(outcome.recordsProgress, "returning to the title with every objective complete records the mission");

    result.objectivesCompleteAtExit = false;
    Check(!MissionOutcomeOf(result).recordsProgress, "with an objective open it records nothing");

    // bossRunTitleStage, as an abort or a death leaves by, never records.
    result.objectivesCompleteAtExit = true;
    result.exit = Frigate::Level::StageExit::RunTitle;
    result.aborted = true;
    outcome = MissionOutcomeOf(result);
    Check(outcome.missionFailedOrAborted && !outcome.recordsProgress, "an abort records nothing");
    result.aborted = false;
    result.bondKilledInAction = true;
    outcome = MissionOutcomeOf(result);
    Check(outcome.bondKilledInAction && !outcome.recordsProgress, "nor does a death");
}

} // namespace

int main() {
    TestOutcomeOfFinishedStage();
    TestObjectiveChecks();
    TestTargetTimes();
    TestEndOfMissionRecording();
    TestStageName();
    TestFailedPage();
    TestCompletePage();
    TestFailedTabs();
    TestCompleteTabs();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::puts("MissionEndTest passed");
    return 0;
}
