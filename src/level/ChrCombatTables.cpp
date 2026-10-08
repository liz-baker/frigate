#include "level/ChrCombatTables.h"

#include "level/TextTable.h"

namespace Frigate::Level {

namespace {

constexpr const char* kSetNames[] = { "rifle",          "pistol",          "doubles",
                                      "crouched_rifle", "crouched_pistol", "crouched_doubles" };
constexpr const char* kRollTable = "D_80030078";
constexpr const char* kWalkTable = "D_80030660";

bool ParseStruck(const std::vector<std::string_view>& fields, std::vector<StruckAnim>* list) {
    if (fields.size() != 10) {
        return false;
    }
    StruckAnim entry;
    entry.anim = std::string(fields[3]);
    if (!ParseNumber(fields[4], entry.flip) || !ParseFloat(fields[5], entry.endFrame) ||
        !ParseFloat(fields[6], entry.speed) || !ParseNumber(fields[7], entry.knockback) ||
        !ParseFloat(fields[8], entry.thudFrame1) || !ParseFloat(fields[9], entry.thudFrame2)) {
        return false;
    }
    list->push_back(std::move(entry));
    return true;
}

bool ParseFiring(const std::vector<std::string_view>& fields, std::vector<FiringAnim>* table) {
    if (fields.size() != 21) {
        return false;
    }
    FiringAnim entry;
    entry.anim = std::string(fields[3]);
    float* slots[] = { &entry.turnEndFrame,   &entry.turnAnglePerFrame, &entry.angleOffset,   &entry.startFrame,
                       &entry.endFrame,       &entry.shootStartFrame,   &entry.shootEndFrame, &entry.recoilStartFrame,
                       &entry.recoilEndFrame, &entry.aimStartFrame,     &entry.aimEndFrame,   &entry.maxUp,
                       &entry.maxDown,        &entry.maxLeft,           &entry.maxRight,      &entry.freeArmFracUp,
                       &entry.freeArmFracDown };
    for (size_t i = 0; i < 17; i++) {
        if (!ParseFloat(fields[4 + i], *slots[i])) {
            return false;
        }
    }
    table->push_back(std::move(entry));
    return true;
}

} // namespace

std::shared_ptr<const ChrCombatTables> ChrCombatTables::Parse(std::string_view text) {
    std::shared_ptr<ChrCombatTables> tables(new ChrCombatTables());
    struct PendingReaction {
        HitReaction reaction;
        std::string death;
        std::string flinch;
    };
    std::vector<PendingReaction> reactions;
    std::vector<std::tuple<size_t, int, std::string>> groupEntries;
    struct PendingMulti {
        size_t slot;
        std::string table;
        size_t index;
    };
    std::vector<PendingMulti> multiEntries;

    for (std::string_view line : Split(text, '\n')) {
        if (line.empty()) {
            continue;
        }
        const auto fields = Split(line, '\t');
        const std::string_view kind = fields[0];
        if (kind == "struck") {
            if (fields.size() < 3 || !ParseStruck(fields, &tables->struck_[std::string(fields[1])])) {
                return nullptr;
            }
        } else if (kind == "reaction") {
            PendingReaction pending;
            if (fields.size() != 9 || !ParseNumber(fields[2], pending.reaction.hitPart) ||
                !ParseNumber(fields[3], pending.reaction.impactPuffCount) ||
                !ParseFloat(fields[4], pending.reaction.impactPuffSize) ||
                !ParseNumber(fields[5], pending.reaction.backPuffCount) ||
                !ParseFloat(fields[6], pending.reaction.backPuffSize)) {
                return nullptr;
            }
            pending.death = std::string(fields[7]);
            pending.flinch = std::string(fields[8]);
            reactions.push_back(std::move(pending));
        } else if (kind == "firing") {
            if (fields.size() < 3 || !ParseFiring(fields, &tables->firing_[std::string(fields[1])])) {
                return nullptr;
            }
        } else if (kind == "group") {
            if (fields.size() != 4) {
                return nullptr;
            }
            size_t set = 0;
            while (set < 6 && fields[1] != kSetNames[set]) {
                set++;
            }
            int slice = 0;
            if (set == 6 || !ParseNumber(fields[2], slice) || slice < 0 || slice >= kGroupsPerSet) {
                return nullptr;
            }
            groupEntries.emplace_back(set, slice, std::string(fields[3]));
        } else if (kind == "multifiring") {
            size_t group = 0;
            size_t stance = 0;
            size_t index = 0;
            MultiFiring entry;
            if (fields.size() != 9 || !ParseNumber(fields[1], group) || group >= kMultiGroups ||
                !ParseNumber(fields[2], stance) || stance >= kMultiStances || !ParseNumber(fields[4], index) ||
                !ParseFloat(fields[6], entry.speedScale) || !ParseFloat(fields[7], entry.loopFrame) ||
                !ParseFloat(fields[8], entry.endFrame)) {
                return nullptr;
            }
            if (fields[5] != "-") {
                entry.anim = std::string(fields[5]);
            }
            const size_t slot = (group * kMultiStances) + stance;
            tables->multi_[slot] = std::move(entry);
            tables->hasMulti_ = true;
            if (fields[3] != "-") {
                multiEntries.push_back({ slot, std::string(fields[3]), index });
            }
        } else if (kind == "explosiondeath") {
            ExplosionDeath death;
            size_t index = 0;
            if (fields.size() != 9 || !ParseNumber(fields[1], index) || index != tables->explosionDeaths_.size() ||
                !ParseNumber(fields[3], death.flip) || !ParseFloat(fields[4], death.speed) ||
                !ParseFloat(fields[5], death.startFrame) || !ParseFloat(fields[6], death.unused) ||
                !ParseFloat(fields[7], death.thudFrame) || !ParseFloat(fields[8], death.endFrame)) {
                return nullptr;
            }
            death.anim = std::string(fields[2]);
            tables->explosionDeaths_.push_back(std::move(death));
        } else if (kind == "explosiongroup") {
            size_t index = 0;
            if (fields.size() < 2 || !ParseNumber(fields[1], index) || index != tables->explosionGroups_.size()) {
                return nullptr;
            }
            std::vector<int> group;
            for (size_t i = 2; i < fields.size(); i++) {
                int entry = 0;
                if (!ParseNumber(fields[i], entry)) {
                    return nullptr;
                }
                group.push_back(entry);
            }
            tables->explosionGroups_.push_back(std::move(group));
        } else if (kind == "impact") {
            int item = 0;
            float force = 0.0f;
            if (fields.size() != 3 || !ParseNumber(fields[1], item) || item < 0 || !ParseFloat(fields[2], force)) {
                return nullptr;
            }
            if (tables->forces_.size() <= static_cast<size_t>(item)) {
                tables->forces_.resize(static_cast<size_t>(item) + 1, 0.0f);
            }
            tables->forces_[static_cast<size_t>(item)] = force;
        } else {
            return nullptr;
        }
    }

    for (PendingReaction& pending : reactions) {
        if (pending.death != "-") {
            const auto found = tables->struck_.find(pending.death);
            if (found == tables->struck_.end()) {
                return nullptr;
            }
            pending.reaction.death = &found->second;
        }
        if (pending.flinch != "-") {
            const auto found = tables->struck_.find(pending.flinch);
            if (found == tables->struck_.end()) {
                return nullptr;
            }
            pending.reaction.flinch = &found->second;
        }
        tables->reactions_.push_back(pending.reaction);
    }
    for (const auto& [set, slice, name] : groupEntries) {
        const auto found = tables->firing_.find(name);
        if (found == tables->firing_.end()) {
            return nullptr;
        }
        tables->groups_[set][static_cast<size_t>(slice)] = &found->second;
    }

    for (const PendingMulti& pending : multiEntries) {
        const auto found = tables->firing_.find(pending.table);
        if (found == tables->firing_.end() || pending.index >= found->second.size()) {
            return nullptr;
        }
        tables->multi_[pending.slot].entry = &found->second[pending.index];
    }

    // The tables the actions index into must all be there.
    if (tables->reactions_.empty() || tables->explosionGroups_.size() != kExplosionDirections ||
        tables->firing_.count(kRollTable) == 0 || tables->firing_.count(kWalkTable) == 0 ||
        tables->struck_.count("death_stagger") == 0) {
        return nullptr;
    }
    for (const auto& set : tables->groups_) {
        for (const auto* group : set) {
            if (group == nullptr) {
                return nullptr;
            }
        }
    }
    return tables;
}

const HitReaction& ChrCombatTables::ReactionFor(int hitPart) const {
    return reactions_[static_cast<size_t>(ReactionIndexFor(hitPart))];
}

int ChrCombatTables::ReactionIndexFor(int hitPart) const {
    for (size_t i = 0; i < reactions_.size(); i++) {
        if (reactions_[i].hitPart == hitPart) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

const std::vector<StruckAnim>& ChrCombatTables::DeathStagger() const {
    return struck_.at("death_stagger");
}

const std::vector<FiringAnim>& ChrCombatTables::Group(FiringSet set, int slice) const {
    return *groups_[static_cast<size_t>(set)][static_cast<size_t>(slice)];
}

const MultiFiring* ChrCombatTables::Multi(int group, int stance) const {
    if (!hasMulti_ || group < 0 || group >= kMultiGroups || stance < 0 || stance >= kMultiStances) {
        return nullptr;
    }
    return &multi_[static_cast<size_t>((group * kMultiStances) + stance)];
}

const std::vector<FiringAnim>& ChrCombatTables::Rolls() const {
    return firing_.at(kRollTable);
}

const std::vector<FiringAnim>& ChrCombatTables::Walks() const {
    return firing_.at(kWalkTable);
}

int ChrCombatTables::IndexIn(const std::vector<FiringAnim>& table, const FiringAnim* anim) {
    if (anim == nullptr || table.empty() || anim < table.data() || anim >= table.data() + table.size()) {
        return -1;
    }
    return static_cast<int>(anim - table.data());
}

const std::vector<int>& ChrCombatTables::ExplosionGroup(int direction) const {
    return explosionGroups_[static_cast<size_t>(direction)];
}

const ExplosionDeath& ChrCombatTables::ExplosionDeathAt(int index) const {
    return explosionDeaths_[static_cast<size_t>(index)];
}

float ChrCombatTables::ForceOfImpact(int item) const {
    return item >= 0 && static_cast<size_t>(item) < forces_.size() ? forces_[static_cast<size_t>(item)] : 0.0f;
}

} // namespace Frigate::Level
