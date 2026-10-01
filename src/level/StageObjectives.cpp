#include <string>

#include "level/StageAi.h"

namespace Frigate::Level {

namespace {

// LMISC's MISC_STR_2C_OBJECTIVE and the three status strings after it.
constexpr int kStringObjective = 0x2C;
constexpr int kStringCompleted = 0x2D;
constexpr int kStringIncomplete = 0x2E;
constexpr int kStringFailed = 0x2F;

} // namespace

ObjectiveWorld Stage::MakeObjectiveWorld() const {
    ObjectiveWorld world;
    world.links = &links_;
    world.inventory = &intro_.inventory;
    world.stageFlags = ai_ ? ai_->State().objectiveRegisters : 0;
    // TODO(https://github.com/liz-baker/frigate/issues/235): get_keyanalyzer_flag, which
    // the key analyser's copy sets.
    world.copiedGoldenEye = false;
    world.difficulty = difficulty_;
    return world;
}

void Stage::TickObjectiveText() {
    // display_objective_status_text_on_status_change; a stage with several
    // players shows none.
    for (const Objectives::Change& change : objectives_.UpdateStatuses(MakeObjectiveWorld())) {
        std::string text(lang_.Get(StringId(TextBank::Misc, kStringObjective)));
        text += ' ';
        text += static_cast<char>('a' + change.index);
        text += ": ";
        int status = kStringIncomplete;
        if (change.status == ObjectiveStatus::Complete) {
            status = kStringCompleted;
        } else if (change.status == ObjectiveStatus::Failed) {
            status = kStringFailed;
        }
        text += lang_.Get(StringId(TextBank::Misc, status));
        hud_.BottomShow(text);
    }
}

int Stage::AiBridge::ObjectiveCount() const {
    return stage_.objectives_.Count();
}

bool Stage::AiBridge::ObjectiveComplete(int objective) const {
    return stage_.objectives_.Status(objective, stage_.MakeObjectiveWorld()) == ObjectiveStatus::Complete;
}

bool Stage::AiBridge::AllObjectivesComplete() const {
    return stage_.objectives_.AllComplete(stage_.MakeObjectiveWorld());
}

} // namespace Frigate::Level
