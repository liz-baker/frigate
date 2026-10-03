#pragma once

#include <array>
#include <cstdint>

#include "level/Objectives.h"

namespace Frigate::Level {

// How a stage hands control back to the front-end: bossReturnTitleStage
// (which first records the completed mission into the folder) or
// bossRunTitleStage (an abort, a death, or a demo's fade out).
enum class StageExit { None, ReturnTitle, RunTitle };

// What a finished stage leaves for the mission screens to read, as globals:
// getMissiontimer(), get_curplay_killcount(), get_curplayer_shot_register(),
// array_favweapon[0], the objectives' statuses, g_isBondKIA and
// mission_failed_or_aborted.
struct StageResult {
    StageExit exit = StageExit::None;
    // Ticks (60 to the second).
    int32_t missionTimer = 0;
    int32_t killCount = 0;
    // The SHOT_REGISTER slots.
    std::array<int32_t, 7> shots{};
    // GUNRIGHT, GUNLEFT.
    std::array<int32_t, 2> favouriteWeapon{};
    // The statuses the screens read once the stage is unloaded: the objectives
    // flagged to complete on exit count as complete.
    std::array<ObjectiveStatus, Objectives::kMax> objectives{};
    // objectiveIsAllComplete() when the stage ended, ahead of that cleanup.
    bool objectivesCompleteAtExit = false;
    bool bondKilledInAction = false;
    bool aborted = false;
};

} // namespace Frigate::Level
