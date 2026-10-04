#include "level/Objectives.h"

namespace Frigate::Level {

namespace {

constexpr uint8_t Type(PropDefType type) {
    return static_cast<uint8_t>(type);
}

} // namespace

void Objectives::Reset() {
    defs_ = nullptr;
    count_ = -1;
    present_.fill(false);
    entries_.fill({});
    statuses_.fill(ObjectiveStatus::Incomplete);
    words_.clear();
    enterRoom_.clear();
    deposit_.clear();
    pictures_.clear();
}

bool Objectives::CompletesOnExit(size_t objective) const {
    if (!present_[objective] || entries_[objective].record >= words_.size()) {
        return false;
    }
    // The byte at 0xD of the objective's record, the third word's second byte.
    const auto flags = static_cast<uint32_t>(words_[entries_[objective].record][2]) >> 16;
    return (flags & 1) != 0;
}

void Objectives::Cleanup() {
    for (size_t i = 0; i < static_cast<size_t>(kMax); i++) {
        if (CompletesOnExit(i) && statuses_[i] != ObjectiveStatus::Failed) {
            statuses_[i] = ObjectiveStatus::Complete;
        }
    }
}

std::array<ObjectiveStatus, Objectives::kMax> Objectives::StatusesOnExit(const ObjectiveWorld& world) const {
    std::array<ObjectiveStatus, kMax> statuses{};
    for (size_t i = 0; i < statuses.size(); i++) {
        statuses[i] = Status(static_cast<int>(i), world);
        if (CompletesOnExit(i) && statuses[i] != ObjectiveStatus::Failed) {
            statuses[i] = ObjectiveStatus::Complete;
        }
    }
    return statuses;
}

void Objectives::Build(const StageSetup& setup) {
    defs_ = &setup.PropDefs();
    words_.assign(defs_->size(), {});
    for (size_t record = 0; record < defs_->size(); record++) {
        const PropDef& def = (*defs_)[record];
        words_[record] = def.objectiveWords;
        if (def.Is(PropDefType::ObjectiveStart)) {
            // add_ptr_to_objective; the start record's word 1 is the objective
            // number and its third word's low byte the difficulty.
            const int number = def.objectiveWords[0];
            if (number < 0 || number >= kMax) {
                continue;
            }
            present_[static_cast<size_t>(number)] = true;
            entries_[static_cast<size_t>(number)] = { record, def.objectiveWords[2] & 0xFF };
            if (count_ < number) {
                count_ = number;
            }
        } else if (def.Is(PropDefType::ObjectiveEnterRoom)) {
            enterRoom_.push_back(record);
        } else if (def.Is(PropDefType::ObjectiveDepositObjectInRoom)) {
            deposit_.push_back(record);
        } else if (def.Is(PropDefType::ObjectivePhotograph)) {
            pictures_.push_back(record);
        }
    }
}

int Objectives::Difficulty(int objective) const {
    if (objective >= 0 && objective < kMax && present_[static_cast<size_t>(objective)]) {
        return entries_[static_cast<size_t>(objective)].difficulty;
    }
    return 0;
}

int32_t Objectives::TextId(int objective) const {
    if (objective < 0 || objective >= kMax || !present_[static_cast<size_t>(objective)]) {
        return 0;
    }
    // The start record's second word is the reserved half and the text id.
    return words_[entries_[static_cast<size_t>(objective)].record][1] & 0xFFFF;
}

int32_t Objectives::BriefingTextId(int page) const {
    if (defs_ == nullptr) {
        return -1;
    }
    // The records are linked newest first, so the last one in the file wins.
    for (size_t record = defs_->size(); record-- > 0;) {
        if ((*defs_)[record].Is(PropDefType::WatchMenuObjectiveText) && words_[record][0] == page) {
            return words_[record][1] & 0xFFFF;
        }
    }
    return -1;
}

ObjectiveStatus Objectives::StatusOf(const Entry& entry, const ObjectiveWorld& world) const {
    ObjectiveStatus status = ObjectiveStatus::Complete;
    for (size_t record = entry.record; record < defs_->size(); record++) {
        const PropDef& def = (*defs_)[record];
        if (def.type == Type(PropDefType::ObjectiveEnd)) {
            break;
        }
        const std::array<int32_t, 3>& words = words_[record];
        ObjectiveStatus current = ObjectiveStatus::Complete;

        // objFindByTagId, with objIsHealthy; a door has no destroyed state yet.
        const auto tagged = [&]() { return world.links->FindByTag(static_cast<uint16_t>(words[0])); };
        const auto healthy = [](const LinkTarget& target) {
            return target.object == nullptr || Objects::IsHealthy(*target.object);
        };
        const auto held = [&](const LinkTarget& target) {
            for (const InvItem& item : world.inventory->Items()) {
                if (item.IsProp() && item.prop == target.Prop()) {
                    return true;
                }
            }
            return false;
        };

        switch (static_cast<PropDefType>(def.type)) {
            case PropDefType::ObjectiveDestroyObject: {
                const LinkTarget target = tagged();
                if (target.Exists() && target.Prop() >= 0 && healthy(target)) {
                    current = ObjectiveStatus::Incomplete;
                }
                break;
            }
            case PropDefType::ObjectiveCompleteCondition:
                if ((world.stageFlags & static_cast<uint32_t>(words[0])) == 0) {
                    current = ObjectiveStatus::Incomplete;
                }
                break;
            case PropDefType::ObjectiveFailCondition:
                if ((world.stageFlags & static_cast<uint32_t>(words[0])) != 0) {
                    current = ObjectiveStatus::Failed;
                }
                break;
            case PropDefType::ObjectiveCollectObject: {
                const LinkTarget target = tagged();
                if (!target.Exists() || target.Prop() < 0 || !healthy(target)) {
                    current = ObjectiveStatus::Failed;
                } else if (!held(target)) {
                    current = ObjectiveStatus::Incomplete;
                }
                break;
            }
            case PropDefType::ObjectiveDepositObject: {
                const LinkTarget target = tagged();
                if (target.Exists() && target.Prop() >= 0 && held(target)) {
                    current = ObjectiveStatus::Incomplete;
                }
                break;
            }
            case PropDefType::ObjectivePhotograph: {
                const LinkTarget target = tagged();
                if (words[1] == 0) {
                    current = !target.Exists() || target.Prop() < 0 || !healthy(target) ? ObjectiveStatus::Failed
                                                                                        : ObjectiveStatus::Incomplete;
                }
                break;
            }
            case PropDefType::ObjectiveEnterRoom:
                if (words[1] == 0) {
                    current = ObjectiveStatus::Incomplete;
                }
                break;
            case PropDefType::ObjectiveDepositObjectInRoom:
                if (words[2] == 0) {
                    current = ObjectiveStatus::Incomplete;
                }
                break;
            case PropDefType::ObjectiveCopyItem:
                if (!world.copiedGoldenEye) {
                    current = ObjectiveStatus::Incomplete;
                }
                break;
            default:
                break;
        }

        if (status == ObjectiveStatus::Complete) {
            if (current != ObjectiveStatus::Complete) {
                status = current;
            }
        } else if (status == ObjectiveStatus::Incomplete && current == ObjectiveStatus::Failed) {
            status = current;
        }
    }
    return status;
}

ObjectiveStatus Objectives::Status(int objective, const ObjectiveWorld& world) const {
    if (objective < 0 || objective >= kMax) {
        return ObjectiveStatus::Complete;
    }
    if (!present_[static_cast<size_t>(objective)] || defs_ == nullptr) {
        return statuses_[static_cast<size_t>(objective)];
    }
    return StatusOf(entries_[static_cast<size_t>(objective)], world);
}

bool Objectives::AllComplete(const ObjectiveWorld& world) const {
    for (int objective = 0; objective < Count(); objective++) {
        if (Difficulty(objective) <= world.difficulty && Status(objective, world) != ObjectiveStatus::Complete) {
            return false;
        }
    }
    return true;
}

std::vector<Objectives::Change> Objectives::UpdateStatuses(const ObjectiveWorld& world) {
    std::vector<Change> changes;
    int available = 0;
    for (int objective = 0; objective <= count_; objective++) {
        const ObjectiveStatus status = Status(objective, world);
        const size_t slot = static_cast<size_t>(objective);
        if (statuses_[slot] != status) {
            statuses_[slot] = status;
            if (Difficulty(objective) <= world.difficulty) {
                changes.push_back({ available, status });
            }
        }
        if (Difficulty(objective) <= world.difficulty) {
            available++;
        }
    }
    return changes;
}

void Objectives::CheckRoomEntered(int room, const StageSetup& setup, const Stan& stan) {
    for (auto it = enterRoom_.rbegin(); it != enterRoom_.rend(); ++it) {
        std::array<int32_t, 3>& words = words_[*it];
        if (words[1] != 0) {
            continue;
        }
        const Pad* pad = setup.FindPad(words[0]);
        if (pad != nullptr && pad->stanTile != kNoTile && room == stan.TileRoom(pad->stanTile)) {
            words[1] = 1;
        }
    }
}

void Objectives::CheckDeposit(int weapon, int room, const StageSetup& setup, const Stan& stan) {
    for (auto it = deposit_.rbegin(); it != deposit_.rend(); ++it) {
        std::array<int32_t, 3>& words = words_[*it];
        if (words[2] != 0 || weapon != words[0]) {
            continue;
        }
        const Pad* pad = setup.FindPad(words[1]);
        if (pad != nullptr && pad->stanTile != kNoTile && room == stan.TileRoom(pad->stanTile)) {
            words[2] = 1;
        }
    }
}

std::vector<Objectives::Picture> Objectives::PendingPictures() const {
    std::vector<Picture> pending;
    for (auto it = pictures_.rbegin(); it != pictures_.rend(); ++it) {
        if (words_[*it][1] == 0) {
            pending.push_back({ *it, static_cast<uint16_t>(words_[*it][0]) });
        }
    }
    return pending;
}

void Objectives::MarkPictureTaken(size_t record) {
    words_[record][1] = 1;
}

} // namespace Frigate::Level
