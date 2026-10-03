#include "game/menu/MissionEndModel.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "game/menu/FileSelectModel.h"
#include "game/menu/MissionFolders.h"
#include "game/menu/MissionSelectModel.h"
#include "game/menu/ObjectiveLines.h"
#include "game/menu/StageName.h"
#include "game/save/SaveData.h"
#include "level/Lang.h"

namespace Frigate::Game::Menu {

namespace {

using Input::Button::kA;
using Input::Button::kB;
using Input::Button::kStart;
using Input::Button::kZ;

constexpr uint16_t kSelectButtons = kStart | kZ | kA;

// GAME_TICKRATE.
constexpr int32_t kTicksPerSecond = 60;

// The Cradle and Aztec rows of LEVEL_SOLO_SEQUENCE the completion screen
// branches on, and the stage the Cradle leads on to.
constexpr int32_t kLevelCradle = Save::kLevelCradle;
constexpr int32_t kLevelAztec = Save::kLevelAztec;

// A stored best time of 0x3FF is 007 mode's marker, not a time.
constexpr int32_t kNoBestTime = 0x3FF;

// solo_target_time_array.
constexpr int32_t kTargetTimes[20][3] = {
    { 0, 160, 0 }, // Dam
    { 0, 0, 125 }, // Facility
    { 300, 0, 0 }, // Runway
    { 0, 210, 0 }, // Surface 1
    { 0, 0, 240 }, // Bunker 1
    { 180, 0, 0 }, // Silo
    { 0, 270, 0 }, // Frigate
    { 0, 0, 255 }, // Surface 2
    { 90, 0, 0 },  // Bunker 2
    { 0, 195, 0 }, // Statue
    { 0, 0, 80 },  // Archives
    { 105, 0, 0 }, // Streets
    { 0, 100, 0 }, // Depot
    { 0, 0, 325 }, // Train
    { 225, 0, 0 }, // Jungle
    { 0, 600, 0 }, // Control
    { 0, 0, 570 }, // Caverns
    { 135, 0, 0 }, // Cradle
    { 0, 540, 0 }, // Aztec
    { 0, 0, 360 }, // Egyptian
};

std::string Format(const char* format, ...) __attribute__((format(printf, 1, 2)));

std::string Format(const char* format, ...) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

std::string MinutesSeconds(int32_t seconds) {
    return Format("%02d:%02d", seconds / 60, seconds % 60);
}

int32_t Percent(int32_t part, int32_t whole) {
    return static_cast<int32_t>(std::floor(((static_cast<float>(part) * 100.0f) / static_cast<float>(whole)) + 0.5f));
}

} // namespace

namespace MissionEnd {

int32_t TargetTime(int32_t mission, int32_t difficulty) {
    if (mission < 0 || mission >= 20 || difficulty < 0 || difficulty > 2) {
        return 0;
    }
    return kTargetTimes[mission][difficulty];
}

bool AllObjectivesComplete(const MissionOutcome& outcome, const MissionContext& context) {
    for (size_t i = 0; i < kObjectivesMax; i++) {
        const BriefingData::Objective& line = context.objectives[i];
        if (line.textId != 0 && line.enabledDifficulty <= context.difficulty &&
            outcome.objectives[i] != Level::ObjectiveStatus::Complete) {
            return false;
        }
    }
    return true;
}

bool AllObjectivesAliveSuccess(const MissionOutcome& outcome, const MissionContext& context) {
    if (outcome.missionFailedOrAborted || outcome.bondKilledInAction) {
        return false;
    }
    for (size_t i = 0; i < kObjectivesMax; i++) {
        const BriefingData::Objective& line = context.objectives[i];
        if (line.textId != 0 && context.difficulty >= line.enabledDifficulty &&
            outcome.objectives[i] != Level::ObjectiveStatus::Complete) {
            return false;
        }
    }
    return true;
}

MissionStatus StatusOf(const MissionOutcome& outcome, const MissionContext& context) {
    if (outcome.bondKilledInAction) {
        return MissionStatus::KilledInAction;
    }
    if (outcome.missionFailedOrAborted) {
        return MissionStatus::Aborted;
    }
    return AllObjectivesAliveSuccess(outcome, context) ? MissionStatus::Completed : MissionStatus::Failed;
}

bool EndOfMissionBriefing(MissionEndSave& save, const MissionOutcome& outcome, const MissionContext& context,
                          bool newCheatUnlocked) {
    if (context.briefingPage < 0 || context.difficulty == Save::kDifficulty007 || context.cheatsAppended) {
        return newCheatUnlocked;
    }
    const int32_t seconds = outcome.missionTimer / kTicksPerSecond;
    const int32_t target = TargetTime(context.missionNum, context.difficulty);
    save.UnlockStage(context.folder, context.missionNum, context.difficulty, seconds);
    if (seconds <= target && !save.IsCheatUnlocked(context.folder, context.missionNum)) {
        save.UnlockCheat(context.folder, context.missionNum);
        return true;
    }
    return false;
}

std::vector<TextLine> StageNameLines(const PageText& text, const MissionContext& context) {
    return Menu::StageNameLines(
        BuildStageName([&text](size_t slot) { return text.Get(TitleId(static_cast<int32_t>(slot))); },
                       context.difficulty, context.briefingPage));
}

std::vector<TextLine> ObjectiveLines(const PageText& text, const MissionOutcome& outcome, const MissionContext& context,
                                     int32_t top) {
    return Menu::ObjectiveLines(text, context.objectives, context.difficulty, top, &outcome.objectives);
}

std::vector<TextLine> MissionFailedLines(const MissionEndText& text, const MissionOutcome& outcome,
                                         const MissionContext& context) {
    std::vector<TextLine> lines = StageNameLines(text, context);
    lines.push_back({ 0x37, 0x8F, text.Get(TitleId(ReportString::kReport)), PageColour::kBlack });

    const std::string heading = text.Get(TitleId(ReportString::kMissionStatus));
    lines.push_back({ 0x37, 0xA7, heading, PageColour::kBlack });

    std::string verdict;
    uint32_t colour = PageColour::kFailedRed;
    switch (StatusOf(outcome, context)) {
        case MissionStatus::KilledInAction:
            verdict = text.Get(TitleId(ReportString::kKia));
            break;
        case MissionStatus::Aborted:
            verdict = text.Get(TitleId(ReportString::kAborted));
            break;
        case MissionStatus::Completed:
            verdict = text.Get(TitleId(ReportString::kCompleted));
            colour = PageColour::kBlack;
            break;
        case MissionStatus::Failed:
            verdict = text.Get(TitleId(ReportString::kFailed));
            break;
    }
    lines.push_back({ text.Measure(heading).width + 0x37, 0xA7, verdict, colour });

    for (TextLine& line : ObjectiveLines(text, outcome, context, 0xBF)) {
        lines.push_back(std::move(line));
    }
    return lines;
}

std::vector<TextLine> MissionCompleteLines(const MissionEndText& text, const MissionOutcome& outcome,
                                           const MissionContext& context, uint32_t bestTimeStored,
                                           bool newCheatUnlocked) {
    namespace S = ShotRegister;

    const int32_t head = outcome.shots[S::kHead];
    const int32_t body = outcome.shots[S::kBody];
    const int32_t limb = outcome.shots[S::kLimb];
    const int32_t gun = outcome.shots[S::kGun];
    const int32_t hat = outcome.shots[S::kHat];
    const int32_t object = outcome.shots[S::kObject];
    const int32_t shotsFired = outcome.shots[S::kTotal];
    int32_t allHits = head + body + limb + gun + hat;
    const int32_t hitShots = allHits + object;
    if (allHits <= 0) {
        allHits = 1;
    }

    std::vector<TextLine> lines = StageNameLines(text, context);

    const std::string statistics = text.Get(TitleId(ReportString::kStatistics));
    lines.push_back({ 0x37, 0x8F, statistics, PageColour::kBlack });
    // The row pitch: the height of the heading, which ends in a line break.
    const int32_t y2 = text.Measure(statistics).height;

    lines.push_back({ 0x37, 0xA7, text.Get(TitleId(ReportString::kTime)), PageColour::kBlack });

    int32_t bestTime = static_cast<int32_t>(bestTimeStored);
    if (bestTime == 0 || !(bestTime < kNoBestTime)) {
        bestTime = -1;
    }
    const int32_t difficulty = context.difficulty >= Save::kDifficulty007 ? Save::kDifficulty00 : context.difficulty;
    const int32_t missionSeconds = outcome.missionTimer / kTicksPerSecond;
    const int32_t targetTime = TargetTime(context.missionNum, difficulty);

    const std::string time = MinutesSeconds(missionSeconds);
    lines.push_back({ 0x82, 0xA7, time, PageColour::kBlack });

    if (newCheatUnlocked) {
        lines.push_back({ 0x82 + text.Measure(time).width, 0xA7,
                          "     [" + text.Get(TitleId(ReportString::kNewCheatAvailable)) + "]", kNewCheatRed });
    }

    const std::string bestLabel = text.Get(TitleId(ReportString::kBestTime));
    if (targetTime > 0 && context.difficulty != Save::kDifficulty007) {
        const int32_t y = y2 + 0xA9;
        lines.push_back({ 0x37, y, text.Get(TitleId(ReportString::kTarget)), PageColour::kBlack });
        const std::string target = MinutesSeconds(targetTime);
        lines.push_back({ 0x82, y, target, PageColour::kBlack });
        if (bestTime >= 0 && bestTime < kNoBestTime) {
            lines.push_back({ 0x82 + text.Measure(target).width, y,
                              "     (" + bestLabel + "  " + MinutesSeconds(bestTime) + ")", PageColour::kBlack });
        }
    } else if (bestTime >= 0 && bestTime < kNoBestTime) {
        const int32_t y = y2 + 0xA9;
        lines.push_back({ 0x37, y, bestLabel, PageColour::kBlack });
        lines.push_back({ 0x82, y, MinutesSeconds(bestTime), PageColour::kBlack });
    }

    lines.push_back({ 0x37, 0xCC, text.Get(TitleId(ReportString::kAccuracy)), PageColour::kBlack });
    const float hitPercent =
        shotsFired > 0 ? (static_cast<float>(hitShots) * 100.0f) / static_cast<float>(shotsFired) : 0.0f;
    lines.push_back({ 0x82, 0xCC, Format("%.1f%%", static_cast<double>(hitPercent)), PageColour::kBlack });

    lines.push_back({ 0x37, 0xDC, text.Get(TitleId(ReportString::kWeaponOfChoice)), PageColour::kBlack });
    std::string weapon = text.WeaponOfChoiceName(outcome.favouriteWeapon[0]);
    if (outcome.favouriteWeapon[0] > 0 && outcome.favouriteWeapon[1] == outcome.favouriteWeapon[0]) {
        // The name's trailing line break gives way to the multiplier.
        if (!weapon.empty()) {
            weapon.pop_back();
        }
        weapon += " x 2\n";
    }
    lines.push_back({ 0xBE, 0xDC, weapon, PageColour::kBlack });

    lines.push_back({ 0x37, 0xF4, text.Get(TitleId(ReportString::kShotTotal)), PageColour::kBlack });
    lines.push_back({ 0x82, 0xF4, Format("%d", shotsFired), PageColour::kBlack });

    const auto hitRow = [&](int32_t slot, int32_t row, int32_t count) {
        const int32_t y = (y2 * row) + 0xF4;
        lines.push_back({ 0xB4, y, text.Get(TitleId(slot)), PageColour::kBlack });
        lines.push_back({ 0x12C, y, Format("%d (%d%%)", count, Percent(count, allHits)), PageColour::kBlack });
    };
    hitRow(ReportString::kHeadHits, 0, head);
    hitRow(ReportString::kBodyHits, 1, body);
    hitRow(ReportString::kLimbHits, 2, limb);
    hitRow(ReportString::kOther, 3, hat + gun);

    lines.push_back({ 0x37, y2 + 0xF4, text.Get(TitleId(ReportString::kKillTotal)), PageColour::kBlack });
    lines.push_back({ 0x82, y2 + 0xF4, Format("%d", outcome.killCount), PageColour::kBlack });
    return lines;
}

} // namespace MissionEnd

namespace {

// The highlight both screens work out first: Previous when the cursor is on
// it, Next otherwise.
void UpdateHighlights(TabState& tabs, const MenuCursor& cursor) {
    tabs.prevHighlight = false;
    tabs.nextHighlight = false;
    if (CursorOnPreviousTab(cursor)) {
        tabs.prevHighlight = true;
    } else {
        tabs.nextHighlight = true;
    }
}

} // namespace

void MissionFailedModel::Init(MenuCursor& cursor) {
    tabs_.nextSelected = false;
    tabs_.prevSelected = false;
    SetCursorToNextTab(cursor);
}

MissionFailedModel::Next MissionFailedModel::Update(const Input::Sample& input, MenuCursor& cursor,
                                                    const MissionContext& context) {
    sfx_.clear();
    UpdateHighlights(tabs_, cursor);

    if (input.Pressed(kSelectButtons)) {
        if (tabs_.prevHighlight) {
            tabs_.prevSelected = true;
            sfx_.push_back(FolderSfx::DoorMetalClose2);
        } else if (tabs_.nextHighlight) {
            tabs_.nextSelected = true;
            sfx_.push_back(FolderSfx::DoorMetalClose2);
        }
    } else if (input.Pressed(kB)) {
        tabs_.prevSelected = true;
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    }

    FileSelectModel::MoveCursorWithStick(input, cursor);

    if (tabs_.nextSelected) {
        return Next::MissionComplete;
    }
    if (tabs_.prevSelected) {
        MissionSelectModel::PlaceCursorOnStage(context.missionNum, cursor);
        return Next::MissionSelect;
    }
    return Next::Stay;
}

bool MissionCompleteModel::Init(bool newCheatUnlocked) {
    tabs_.nextSelected = false;
    tabs_.prevSelected = false;
    return newCheatUnlocked;
}

MissionCompleteModel::Next MissionCompleteModel::Update(const Input::Sample& input, MenuCursor& cursor,
                                                        const MissionOutcome& outcome, const MissionContext& context,
                                                        MissionFlow& flow) {
    sfx_.clear();
    UpdateHighlights(tabs_, cursor);

    if (input.Pressed(kSelectButtons)) {
        if (tabs_.nextHighlight) {
            tabs_.nextSelected = true;
            sfx_.push_back(FolderSfx::DoorMetalClose2);
        } else if (tabs_.prevHighlight) {
            tabs_.prevSelected = true;
            sfx_.push_back(FolderSfx::DoorMetalClose2);
        }
    } else if (input.Pressed(kB)) {
        tabs_.prevSelected = true;
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    }

    FileSelectModel::MoveCursorWithStick(input, cursor);

    if (tabs_.nextSelected) {
        if (MissionEnd::AllObjectivesAliveSuccess(outcome, context) && !context.cheatsAppended) {
            if (context.missionNum == kLevelCradle) {
                flow.selectedStage = static_cast<int32_t>(Level::LevelId::Cuba);
                return Next::RunStage;
            }
            if (context.missionNum >= kLevelAztec) {
                MissionSelectModel::PlaceCursorOnStage(context.missionNum, cursor);
                return Next::MissionSelect;
            }
            // The next part of the chapter, or the first of the next chapter.
            const auto entries = MissionFolderEntries();
            size_t entry = static_cast<size_t>(context.briefingPage + 1);
            while (entries[entry].folderTextSlot != 0) {
                if (static_cast<int32_t>(entries[entry].stage) >= 0) {
                    break;
                }
                entry++;
            }
            if (entries[entry].folderTextSlot != 0) {
                flow.briefingPage = static_cast<int32_t>(entry);
                flow.selectedStage = static_cast<int32_t>(entries[entry].stage);
            }
        }
        return Next::Briefing;
    }
    if (tabs_.prevSelected) {
        MissionSelectModel::PlaceCursorOnStage(context.missionNum, cursor);
        return Next::MissionSelect;
    }
    return Next::Stay;
}

} // namespace Frigate::Game::Menu
