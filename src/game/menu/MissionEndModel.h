#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/menu/BriefingModel.h"
#include "game/menu/FolderTabs.h"
#include "game/menu/PageText.h"
#include "level/Objectives.h"
#include "level/StageResult.h"

namespace Frigate::Game::Menu {

// The SHOT_REGISTER slots of a player's shot_count.
namespace ShotRegister {
constexpr size_t kTotal = 0;
constexpr size_t kHead = 1;
constexpr size_t kBody = 2;
constexpr size_t kLimb = 3;
constexpr size_t kGun = 4;
constexpr size_t kHat = 5;
constexpr size_t kObject = 6;
} // namespace ShotRegister

// What a finished stage leaves for the mission screens to read: the globals
// getMissiontimer(), get_curplay_killcount(), get_curplayer_shot_register(),
// array_favweapon[0], get_status_of_objective(), g_isBondKIA and
// mission_failed_or_aborted.
struct MissionOutcome {
    // Ticks (60 to the second).
    int32_t missionTimer = 0;
    int32_t killCount = 0;
    std::array<int32_t, 7> shots{};
    // The right-hand and left-hand item (GUNRIGHT, GUNLEFT); 0 for none.
    std::array<int32_t, 2> favouriteWeapon{};
    std::array<Level::ObjectiveStatus, kObjectivesMax> objectives{};
    bool bondKilledInAction = false;
    bool missionFailedOrAborted = false;
    // bossReturnTitleStage ran with objectiveIsAllComplete() true, which is
    // what records the mission into the folder; an abort or a death leaves by
    // bossRunTitleStage and records nothing.
    bool recordsProgress = false;
};

// The outcome a finished stage leaves (the Stage's Result()).
MissionOutcome MissionOutcomeOf(const Level::StageResult& result);

// The selection the mission screens report on: briefingpage, the folder and
// difficulty, whether cheats are active, and the stage's objectives.
struct MissionContext {
    // briefingpage: the row of mission_folder_setup_entries, -1 for none.
    int32_t briefingPage = -1;
    // That row's LEVEL_SOLO_SEQUENCE value.
    int32_t missionNum = 0;
    int32_t folder = 0;
    // selected_difficulty (lvlGetSelectedDifficulty is the same value).
    int32_t difficulty = 0;
    // g_AppendCheatSinglePlayer.
    bool cheatsAppended = false;
    std::array<BriefingData::Objective, kObjectivesMax> objectives{};
};

// What the end of a mission records into the player's folder. SaveProgress
// backs it in the game (MissionEnd.cpp).
class MissionEndSave {
  public:
    virtual ~MissionEndSave() = default;
    // The folder's best time in seconds, 0 when none.
    virtual uint32_t BestTime(int32_t folder, int32_t mission, int32_t difficulty) const = 0;
    // fileUnlockStageInFolderAtDifficulty.
    virtual void UnlockStage(int32_t folder, int32_t mission, int32_t difficulty, int32_t seconds) = 0;
    // fileGetIsCheatUnlocked and fileSaveFolderUnlockCheat.
    virtual bool IsCheatUnlocked(int32_t folder, int32_t cheat) const = 0;
    virtual void UnlockCheat(int32_t folder, int32_t cheat) = 0;
};

// What the screens' text needs of the font and the loaded string banks, and
// the name of an item for the weapon of choice.
class MissionEndText : public PageText {
  public:
    // get_ptr_long_watch_text_for_item.
    virtual std::string WeaponOfChoiceName(int32_t item) const = 0;
};

// MissionStatus: the report's verdict.
enum class MissionStatus { KilledInAction, Aborted, Completed, Failed };

namespace MissionEnd {

// The new-cheat notice's text colour (0xRRGGBBAA).
constexpr uint32_t kNewCheatRed = 0xA00000FF;

// LTITLE string slots the report and statistics pages print.
namespace ReportString {
constexpr int32_t kReport = 98;
constexpr int32_t kMissionStatus = 99;
constexpr int32_t kKia = 100;
constexpr int32_t kAborted = 101;
constexpr int32_t kCompleted = 102;
constexpr int32_t kFailed = 103;
constexpr int32_t kStatistics = 104;
constexpr int32_t kTime = 105;
constexpr int32_t kAccuracy = 106;
constexpr int32_t kWeaponOfChoice = 107;
constexpr int32_t kShotTotal = 108;
constexpr int32_t kHeadHits = 109;
constexpr int32_t kBodyHits = 110;
constexpr int32_t kLimbHits = 111;
constexpr int32_t kOther = 112;
constexpr int32_t kKillTotal = 113;
constexpr int32_t kBestTime = 273;
constexpr int32_t kTarget = 274;
constexpr int32_t kNewCheatAvailable = 275;
} // namespace ReportString

// solo_target_time_array: the par time in seconds of a mission at Agent,
// Secret Agent or 00 Agent; 0 where the mission has none at that difficulty.
int32_t TargetTime(int32_t mission, int32_t difficulty);

// objectiveIsAllComplete: every objective that applies at the difficulty is
// complete.
bool AllObjectivesComplete(const MissionOutcome& outcome, const MissionContext& context);

// frontCompleteAllObjectivesAliveSuccess.
bool AllObjectivesAliveSuccess(const MissionOutcome& outcome, const MissionContext& context);

MissionStatus StatusOf(const MissionOutcome& outcome, const MissionContext& context);

// end_of_mission_briefing: records the time (and the lower difficulties') and
// unlocks the mission's cheat when the par time was beaten. Returns the new
// g_NewCheatUnlocked, which it only changes when it records anything.
bool EndOfMissionBriefing(MissionEndSave& save, const MissionOutcome& outcome, const MissionContext& context,
                          bool newCheatUnlocked);

// print_current_solo_briefing_stage_name: the difficulty, chapter and part
// lines at the top of the paper.
std::vector<TextLine> StageNameLines(const PageText& text, const MissionContext& context);

// print_objectives_and_status_to_menu with its status argument set, as both
// mission screens call it, from row `top`.
std::vector<TextLine> ObjectiveLines(const PageText& text, const MissionOutcome& outcome, const MissionContext& context,
                                     int32_t top);

// constructor_menu0C_missionfailed's paper, less the tabs and the cursor.
std::vector<TextLine> MissionFailedLines(const MissionEndText& text, const MissionOutcome& outcome,
                                         const MissionContext& context);

// constructor_menu0D_missioncomplete's paper. `bestTime` is the folder's
// stored time for the mission now.
std::vector<TextLine> MissionCompleteLines(const MissionEndText& text, const MissionOutcome& outcome,
                                           const MissionContext& context, uint32_t bestTime, bool newCheatUnlocked);

} // namespace MissionEnd

// The values the mission screens hand on (briefingpage and selected_stage) when
// they move to the next mission.
struct MissionFlow {
    int32_t briefingPage = -1;
    // selected_stage, a LevelId value.
    int32_t selectedStage = -1;
};

// The side tabs the two screens read: which one the cursor is on, and which
// has been chosen.
struct TabState {
    bool nextHighlight = false;
    bool prevHighlight = false;
    bool nextSelected = false;
    bool prevSelected = false;
};

// init_menu0C_missionfailed and interface_menu0C_missionfailed, apart from the
// drawing: the report page with Next to the statistics and Previous back to
// the mission list.
class MissionFailedModel {
  public:
    enum class Next { Stay, MissionSelect, MissionComplete };

    // The tabs are cleared and the cursor goes onto Next.
    void Init(MenuCursor& cursor);
    Next Update(const Input::Sample& input, MenuCursor& cursor, const MissionContext& context);

    const TabState& Tabs() const {
        return tabs_;
    }
    const std::vector<FolderSfx>& Sfx() const {
        return sfx_;
    }

  private:
    TabState tabs_;
    std::vector<FolderSfx> sfx_;
};

// init_menu0D_missioncomplete and interface_menu0D_missioncomplete, apart from
// the drawing: the statistics page. Next carries on to the following mission's
// briefing after a clean completion, or retries this one; Previous goes back
// to the mission list.
class MissionCompleteModel {
  public:
    enum class Next { Stay, MissionSelect, Briefing, RunStage };

    // The tabs are cleared. Returns whether the new-cheat sound plays.
    bool Init(bool newCheatUnlocked);
    // `flow` is briefingpage and selected_stage, which moving on to the next
    // mission sets.
    Next Update(const Input::Sample& input, MenuCursor& cursor, const MissionOutcome& outcome,
                const MissionContext& context, MissionFlow& flow);

    const TabState& Tabs() const {
        return tabs_;
    }
    const std::vector<FolderSfx>& Sfx() const {
        return sfx_;
    }

  private:
    TabState tabs_;
    std::vector<FolderSfx> sfx_;
};

} // namespace Frigate::Game::Menu
