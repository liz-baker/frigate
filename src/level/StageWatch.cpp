#include "level/StageWatch.h"

#include "level/StageAi.h"

namespace Frigate::Level {

namespace {

// PROPFLAG2_00040000: an object carrying it is kept in the inventory but not
// listed by the watch.
constexpr uint32_t kFlags2NotListed = 0x40000;

// LMISC_STRINGS: what the briefing page says for a page the stage has no text
// for.
constexpr int32_t kMiscError = 0x29;
constexpr int32_t kMiscNoBriefing = 0x2A;
constexpr int32_t kMiscBlank = 0x2B;

} // namespace

std::string_view Stage::WatchBridge::Text(int32_t id) const {
    return stage_.lang_.Get(id);
}

float Stage::WatchBridge::Health() const {
    return stage_.health_.Health();
}

float Stage::WatchBridge::Armour() const {
    return stage_.health_.Armour();
}

int32_t Stage::WatchBridge::Difficulty() const {
    return stage_.difficulty_;
}

int32_t Stage::WatchBridge::ObjectiveCount() const {
    return stage_.objectives_.Count();
}

int32_t Stage::WatchBridge::ObjectiveDifficulty(int32_t objective) const {
    return stage_.objectives_.Difficulty(objective);
}

std::string_view Stage::WatchBridge::ObjectiveText(int32_t objective) const {
    return stage_.lang_.Get(stage_.objectives_.TextId(objective));
}

ObjectiveStatus Stage::WatchBridge::ObjectiveStatusOf(int32_t objective) const {
    return stage_.objectives_.Status(objective, stage_.MakeObjectiveWorld());
}

bool Stage::WatchBridge::AllObjectivesComplete() const {
    return stage_.objectives_.AllComplete(stage_.MakeObjectiveWorld());
}

std::string_view Stage::WatchBridge::BriefingPage(int32_t page) const {
    const int32_t id = stage_.objectives_.BriefingTextId(page);
    if (id >= 0) {
        return stage_.lang_.Get(id);
    }
    if (page == 0) {
        return stage_.lang_.Get(StringId(TextBank::Misc, kMiscError));
    }
    if (page == 1) {
        return stage_.lang_.Get(StringId(TextBank::Misc, kMiscNoBriefing));
    }
    return stage_.lang_.Get(StringId(TextBank::Misc, kMiscBlank));
}

bool Stage::WatchBridge::PropListed(const InvItem& item) const {
    if (item.prop < 0) {
        return false;
    }
    const PlacedObject* object = stage_.objects_.ForProp(item.prop);
    if (object == nullptr) {
        return false;
    }
    switch (stage_.objects_.Props()[item.prop].type) {
        case PropType::Weapon:
            return (object->runtimeFlags & ObjectRuntime::Destroyed) != 0;
        case PropType::Obj:
            return (object->flags2 & kFlags2NotListed) == 0;
        default:
            return false;
    }
}

void Stage::WatchBridge::AbortMission() {
    // set_missionstate(MISSION_STATE_0), bossRunTitleStage and
    // mission_failed_or_aborted: the game returns to the title.
    stage_.music_.Stop();
    stage_.levelEndRequested_ = true;
}

void Stage::WatchBridge::EnterWatchMusic() {
    stage_.music_.EnterWatch();
}

void Stage::WatchBridge::LeaveWatchMusic() {
    stage_.music_.LeaveWatch();
}

} // namespace Frigate::Level
