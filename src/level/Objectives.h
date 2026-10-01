#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "level/Inventory.h"
#include "level/PropLinks.h"
#include "level/StageSetup.h"

namespace Frigate::Level {

// OBJECTIVESTATUS.
enum class ObjectiveStatus : int {
    Incomplete,
    Complete,
    Failed,
};

// What an objective's criteria read of the level.
struct ObjectiveWorld {
    const PropLinks* links = nullptr;
    const Inventory* inventory = nullptr;
    // objectiveregisters1, the stage flags the AI sets.
    uint32_t stageFlags = 0;
    // get_keyanalyzer_flag: the key analyser has copied the GoldenEye key.
    bool copiedGoldenEye = false;
    // lvlGetSelectedDifficulty.
    int difficulty = 0;
};

// The stage's objectives (objective.c and objective_status.c): the setup's
// objective records, their runtime state and the status they work out.
class Objectives {
  public:
    // OBJECTIVES_MAX.
    static constexpr int kMax = 10;

    // something_with_stage_objectives, then the passes proplvreset2 makes over
    // the records: every objective, enter-room, deposit and photograph
    // criterion is registered.
    void Reset();
    void Build(const StageSetup& setup);

    // objectiveGetCount.
    int Count() const {
        return count_ + 1;
    }
    // get_difficulty_for_objective: the lowest difficulty objective `objective`
    // applies to, 0 for none.
    int Difficulty(int objective) const;
    // get_status_of_objective.
    ObjectiveStatus Status(int objective, const ObjectiveWorld& world) const;
    // objectiveIsAllComplete.
    bool AllComplete(const ObjectiveWorld& world) const;

    // One status change display_objective_status_text_on_status_change reports:
    // the objective's position among those of the difficulty, and its new status.
    struct Change {
        int index = 0;
        ObjectiveStatus status = ObjectiveStatus::Incomplete;
    };
    // The statuses that changed since the last call, in objective order.
    std::vector<Change> UpdateStatuses(const ObjectiveWorld& world);

    // objectivestatusCheckRoomEntered and objectivestatusCheckDeposit.
    void CheckRoomEntered(int room, const StageSetup& setup, const Stan& stan);
    void CheckDeposit(int weapon, int room, const StageSetup& setup, const Stan& stan);

    // The photograph criteria still to be taken: the tag of the object to
    // photograph, and a way to mark it taken. objectiveTakePictureHandler
    // walks them.
    struct Picture {
        size_t record = 0;
        uint16_t tag = 0;
    };
    std::vector<Picture> PendingPictures() const;
    void MarkPictureTaken(size_t record);

  private:
    struct Entry {
        // The propdef record of the objective's start.
        size_t record = 0;
        int difficulty = 0;
    };

    ObjectiveStatus StatusOf(const Entry& entry, const ObjectiveWorld& world) const;

    const std::vector<PropDef>* defs_ = nullptr;
    int count_ = -1;
    std::array<bool, kMax> present_{};
    std::array<Entry, kMax> entries_{};
    std::array<ObjectiveStatus, kMax> statuses_{};
    // The criteria's runtime words (the words after the type word), by record.
    std::vector<std::array<int32_t, 3>> words_;
    std::vector<size_t> enterRoom_;
    std::vector<size_t> deposit_;
    std::vector<size_t> pictures_;
};

} // namespace Frigate::Level
