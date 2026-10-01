#include "level/PropList.h"

#include <algorithm>

#include "level/BgLevel.h"
#include "level/Stan.h"

namespace Frigate::Level {

namespace {

constexpr int16_t kFreeBlock = -2;
constexpr int16_t kEmptySlot = -1;
// A block holds this many props; its last slot links to the next block.
constexpr int kPropSlots = PropList::kBlockSlots - 1;
constexpr int kLinkSlot = PropList::kBlockSlots - 1;
// chrpropUpdateRoomList's most rooms.
constexpr int kMaxUpdatedRooms = 7;

} // namespace

void PropList::Reset(int roomCount, Stan* stan, const BgLevel* level) {
    stan_ = stan;
    level_ = level;
    props_.assign(kMaxProps, Prop{});
    free_.clear();
    for (int i = kMaxProps - 1; i >= 0; i--) {
        free_.push_back(i);
    }
    active_.clear();

    lookup_.clear();
    roomBlocks_.assign(static_cast<size_t>(roomCount), -1);
    blocks_.assign(kBlockCount, {});
    for (auto& block : blocks_) {
        block.fill(kEmptySlot);
        block[0] = kFreeBlock;
    }
}

int PropList::Allocate() {
    if (free_.empty()) {
        return -1;
    }
    const int prop = free_.back();
    free_.pop_back();
    Prop& record = (*this)[prop];
    record = Prop{};
    record.rooms[0] = kNoRoom;
    return prop;
}

void PropList::Free(int prop) {
    (*this)[prop].stan = kNoTile;
    free_.push_back(prop);
}

void PropList::Activate(int prop) {
    active_.push_back(prop);
}

int PropList::ActiveBefore(int prop) const {
    const auto at = std::find(active_.begin(), active_.end(), prop);
    return at == active_.end() || at == active_.begin() ? -1 : *(at - 1);
}

bool PropList::IsActive(int prop) const {
    return std::find(active_.begin(), active_.end(), prop) != active_.end();
}

void PropList::ActivateThisFrame(int prop) {
    active_.insert(active_.begin(), prop);
}

void PropList::Delist(int prop) {
    active_.erase(std::remove(active_.begin(), active_.end(), prop), active_.end());
}

void PropList::Reparent(int child, int host) {
    Prop& newChild = (*this)[child];
    Prop& parent = (*this)[host];
    newChild.parent = host;
    if (parent.child >= 0) {
        (*this)[parent.child].next = child;
    }
    newChild.prev = parent.child;
    newChild.next = -1;
    newChild.stan = kNoTile;
    parent.child = child;
}

void PropList::Detach(int prop) {
    Prop& record = (*this)[prop];
    if (record.parent < 0) {
        return;
    }
    Prop& parent = (*this)[record.parent];
    if (parent.child == prop) {
        parent.child = record.prev;
    }
    if (record.prev >= 0) {
        (*this)[record.prev].next = record.next;
    }
    if (record.next >= 0) {
        (*this)[record.next].prev = record.prev;
    }
    record.parent = -1;
    record.prev = -1;
    record.next = -1;
}

bool PropList::InsertPropnum(int propnum, int block) {
    auto& slots = blocks_[static_cast<size_t>(block)];
    for (int i = 0; i < kPropSlots; i++) {
        if (slots[static_cast<size_t>(i)] < 0) {
            slots[static_cast<size_t>(i)] = static_cast<int16_t>(propnum);
            return true;
        }
    }
    return false;
}

// chrpropInitializeNewChunkForRoom: takes a free block for the room, linked
// after `previousBlock` (or as the room's first when that is -1).
int PropList::NewBlockForRoom(int room, int previousBlock) {
    for (int i = 0; i < kBlockCount; i++) {
        auto& slots = blocks_[static_cast<size_t>(i)];
        if (slots[0] == kFreeBlock) {
            slots.fill(kEmptySlot);
            if (previousBlock >= 0) {
                blocks_[static_cast<size_t>(previousBlock)][kLinkSlot] = static_cast<int16_t>(i);
            } else {
                roomBlocks_[static_cast<size_t>(room)] = static_cast<int16_t>(i);
            }
            return i;
        }
    }
    return -1;
}

void PropList::RegisterRoom(int prop, int room) {
    if (room < 0 || static_cast<size_t>(room) >= roomBlocks_.size()) {
        return;
    }
    int previous = -1;
    int block = roomBlocks_[static_cast<size_t>(room)];
    while (block >= 0) {
        if (InsertPropnum(prop, block)) {
            return;
        }
        previous = block;
        block = blocks_[static_cast<size_t>(block)][kLinkSlot];
    }
    block = NewBlockForRoom(room, previous);
    if (block >= 0) {
        InsertPropnum(prop, block);
    }
}

void PropList::DeregisterRoom(int prop, int room) {
    if (room < 0 || static_cast<size_t>(room) >= roomBlocks_.size()) {
        return;
    }
    int previous = -1;
    int block = roomBlocks_[static_cast<size_t>(room)];
    bool removed = false;
    while (block >= 0) {
        auto& slots = blocks_[static_cast<size_t>(block)];
        bool populated = false;
        for (int i = 0; i < kPropSlots; i++) {
            if (slots[static_cast<size_t>(i)] == prop) {
                slots[static_cast<size_t>(i)] = kEmptySlot;
                removed = true;
            } else if (!populated && slots[static_cast<size_t>(i)] >= 0) {
                populated = true;
            }
        }
        if (!populated) {
            // An empty block is freed and unlinked from the room's chain.
            slots[0] = kFreeBlock;
            if (previous >= 0) {
                blocks_[static_cast<size_t>(previous)][kLinkSlot] = slots[kLinkSlot];
            } else {
                roomBlocks_[static_cast<size_t>(room)] = slots[kLinkSlot];
            }
        } else {
            previous = block;
        }
        if (removed) {
            return;
        }
        block = slots[kLinkSlot];
    }
}

void PropList::RegisterRooms(int prop) {
    const Prop& record = (*this)[prop];
    for (size_t i = 0; i < record.rooms.size() && record.rooms[i] != kNoRoom; i++) {
        RegisterRoom(prop, record.rooms[i]);
    }
}

void PropList::DeregisterRooms(int prop) {
    Prop& record = (*this)[prop];
    for (size_t i = 0; i < record.rooms.size() && record.rooms[i] != kNoRoom; i++) {
        DeregisterRoom(prop, record.rooms[i]);
    }
    if ((record.flags & PropFlag::KeepRoomList) == 0) {
        record.rooms[0] = kNoRoom;
    }
}

void PropList::UpdateRoomList(int prop, const Vec3& boxMin, const Vec3& boxMax, float radius) {
    Prop& record = (*this)[prop];
    std::array<int, kMaxUpdatedRooms> rooms{};
    int count = 0;

    if ((record.flags & PropFlag::SeedFromRooms) != 0) {
        // TODO(https://github.com/liz-baker/frigate/issues/214): a projectile
        // in flight seeds from its own room list (Projectile::unkCC) instead.
        while (count < kMaxUpdatedRooms && record.rooms[static_cast<size_t>(count)] != kNoRoom) {
            rooms[static_cast<size_t>(count)] = record.rooms[static_cast<size_t>(count)];
            count++;
        }
    } else if (record.stan != kNoTile) {
        stan_->TestCircleRooms(record.stan, record.position[0], record.position[2], radius, rooms.data(), &count,
                               kMaxUpdatedRooms);
    }

    level_->AddRoomsThroughPortals(boxMin, boxMax, rooms.data(), &count, kMaxUpdatedRooms);

    for (int i = 0; i < count; i++) {
        record.rooms[static_cast<size_t>(i)] = static_cast<uint8_t>(rooms[static_cast<size_t>(i)]);
    }
    record.rooms[static_cast<size_t>(count)] = kNoRoom;
}

void PropList::Defrag() {
    for (size_t room = 0; room < roomBlocks_.size(); room++) {
        int previous = roomBlocks_[room];
        if (previous < 0) {
            continue;
        }
        int next = blocks_[static_cast<size_t>(previous)][kLinkSlot];
        while (next >= 0) {
            auto& into = blocks_[static_cast<size_t>(previous)];
            for (int j = 0; j < kPropSlots; j++) {
                if (into[static_cast<size_t>(j)] >= 0) {
                    continue;
                }
                auto& from = blocks_[static_cast<size_t>(next)];
                int k = 0;
                for (; k < kPropSlots; k++) {
                    if (from[static_cast<size_t>(k)] >= 0) {
                        into[static_cast<size_t>(j)] = from[static_cast<size_t>(k)];
                        from[static_cast<size_t>(k)] = kEmptySlot;
                        break;
                    }
                }
                for (; k < kPropSlots; k++) {
                    if (from[static_cast<size_t>(k)] >= 0) {
                        break;
                    }
                }
                if (k == kPropSlots) {
                    // The later block is empty: free it and unlink it.
                    from[0] = kFreeBlock;
                    into[kLinkSlot] = from[kLinkSlot];
                    next = into[kLinkSlot];
                    if (next < 0) {
                        break;
                    }
                }
            }
            if (next >= 0) {
                previous = next;
                next = blocks_[static_cast<size_t>(next)][kLinkSlot];
            }
        }
    }
}

const std::vector<int16_t>& PropList::RoomProps(const int* rooms) const {
    lookup_.clear();
    for (; *rooms >= 0; rooms++) {
        if (static_cast<size_t>(*rooms) >= roomBlocks_.size()) {
            continue;
        }
        for (int block = roomBlocks_[static_cast<size_t>(*rooms)]; block >= 0;
             block = blocks_[static_cast<size_t>(block)][kLinkSlot]) {
            for (int i = 0; i < kPropSlots; i++) {
                const int16_t prop = blocks_[static_cast<size_t>(block)][static_cast<size_t>(i)];
                if (prop >= 0 && std::find(lookup_.begin(), lookup_.end(), prop) == lookup_.end() &&
                    lookup_.size() < static_cast<size_t>(kLookupLength - 1)) {
                    lookup_.push_back(prop);
                }
            }
        }
    }
    return lookup_;
}

} // namespace Frigate::Level
