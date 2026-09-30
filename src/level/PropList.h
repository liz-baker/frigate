#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "level/BgHeader.h"
#include "level/StanData.h"

namespace Frigate::Level {

class BgLevel;
class Stan;

// The values the game uses for PROP_TYPE.
enum class PropType : uint8_t {
    Nul,
    Obj,
    Door,
    Chr,
    Weapon,
    Player,
    Viewer,
    Explosion,
    Smoke,
};

// The bits of PropRecord::flags.
namespace PropFlag {
// PROPFLAG_RENDERPOSTBG: rendered in chrpropsRenderPass's second (post-bg)
// pass rather than its first.
constexpr uint8_t RenderPostBg = 0x01;
// PROPFLAG_ONSCREEN: a candidate for this frame's on-screen prop list.
constexpr uint8_t OnScreen = 0x02;
// PROPFLAG_ENABLED.
constexpr uint8_t Enabled = 0x04;
// The prop's room list is what its rooms are recalculated from, not the
// stan tile locus around it.
constexpr uint8_t SeedFromRooms = 0x08;
// Deregistering the prop from its rooms leaves its room list as it was.
constexpr uint8_t KeepRoomList = 0x10;
// PROPFLAG_00000020: chrpropsRenderPass splits its first pass on this bit
// alongside RenderPostBg; the decomp does not name what it means there.
constexpr uint8_t Unnamed0x20 = 0x20;
} // namespace PropFlag

// A room list ends at this entry.
constexpr uint8_t kNoRoom = 0xFF;

// PropRecord: what the game keeps for everything that has a position in the
// stage.
struct Prop {
    PropType type = PropType::Nul;
    uint8_t flags = 0;
    // The record the prop belongs to: an index into the stage's objects for
    // an Obj, or the character's slot for a Chr; -1 for none.
    int object = -1;
    Vec3 position{};
    // stan: the tile the prop stands on.
    int stan = kNoTile;
    // The on-screen prop list's sort key: how far the prop sits from the
    // camera along its view axis (-render_pos.m[3][2]). Only meaningful while
    // PropFlag::OnScreen is set.
    float zDepth = 0.0f;
    // timetoregen: ticks until a respawning object is back to full opacity,
    // counting down; 0 for one that is not respawning.
    // TODO(https://github.com/liz-baker/frigate/issues/224): the respawn that sets it.
    int16_t timeToRegen = 0;
    // The rooms the prop overlaps, ended by kNoRoom.
    std::array<uint8_t, 8> rooms{};
    // The prop it is attached to, its newest attached prop (child), and its
    // neighbours in the parent's list (prev is the older one); -1 for none.
    int parent = -1;
    int child = -1;
    int prev = -1;
    int next = -1;
};

// The prop pool and the per-room lists of the props in each room
// (init_load_objpos_table, chrpropAllocate and the room registration of
// chrprop.c).
class PropList {
  public:
    // MAX_PROPS.
    static constexpr int kMaxProps = 600;
    // PTR_LIST_OBJECT_LOOKUP_INDICES_LEN: the most props one room lookup holds.
    static constexpr int kLookupLength = 512;
    // BSS_8007161C_LEN and BSS_8007161C_DATA_LEN: a room's props live in
    // chains of blocks; the last of a block's slots links to the next block.
    static constexpr int kBlockCount = 256;
    static constexpr int kBlockSlots = 16;

    // init_load_objpos_table: empties the pool and every room's list.
    // `stan` and `level` are what UpdateRoomList reads.
    void Reset(int roomCount, Stan* stan, const BgLevel* level);

    // chrpropAllocate: the new prop's slot, or -1 when the pool is used up.
    int Allocate();
    // chrpropFree.
    void Free(int prop);
    Prop& operator[](int prop) {
        return props_[static_cast<size_t>(prop)];
    }
    const Prop& operator[](int prop) const {
        return props_[static_cast<size_t>(prop)];
    }

    // chrpropActivate: the prop joins the end of the active list.
    void Activate(int prop);
    // chrpropActivateThisFrame: the prop joins the start of the active list, the
    // last place the frame's walk from the end reaches, so it is ticked this frame.
    void ActivateThisFrame(int prop);
    // chrpropDelist: the prop leaves the active list.
    void Delist(int prop);
    // chrpropReparent: `child` becomes the newest prop attached to `host`. It
    // loses its own tile, and is not in the active list (see Delist).
    void Reparent(int child, int host);
    // chrpropDetach: `prop` leaves the props attached to its parent.
    void Detach(int prop);
    // chrpropEnable and chrpropDisable.
    void Enable(int prop) {
        (*this)[prop].flags |= PropFlag::Enabled;
    }
    void Disable(int prop) {
        (*this)[prop].flags &= static_cast<uint8_t>(~PropFlag::Enabled);
    }
    // The active props, oldest first (from g_ActivePropsHead to the tail).
    const std::vector<int>& Active() const {
        return active_;
    }

    // chrpropRegisterRoom and chrpropDeregisterRoom: adds or removes the prop
    // in one room's list.
    void RegisterRoom(int prop, int room);
    void DeregisterRoom(int prop, int room);
    // chrpropRegisterRooms and chrpropDeregisterRooms: every room in the
    // prop's room list.
    void RegisterRooms(int prop);
    void DeregisterRooms(int prop);

    // chrpropUpdateRoomList: recalculates the prop's rooms from the box
    // (world units) it fills, `radius` around its position.
    void UpdateRoomList(int prop, const Vec3& boxMin, const Vec3& boxMax, float radius);

    // propsDefragRoomProps: moves props from the end of each room's chain of
    // blocks into free slots earlier in it, and frees the blocks that empties.
    void Defrag();

    // roomGetProps: the props in any of `rooms` (a -1-terminated list), each
    // once, in room order.
    const std::vector<int16_t>& RoomProps(const int* rooms) const;

  private:
    int NewBlockForRoom(int room, int previousBlock);
    bool InsertPropnum(int propnum, int block);

    std::vector<Prop> props_;
    // The free slots; the next to allocate is the last.
    std::vector<int> free_;
    std::vector<int> active_;
    // The first block of each room's list, -1 for none. A block's first slot
    // is -2 while the block is free, and a slot is -1 while it is empty.
    std::vector<int16_t> roomBlocks_;
    std::vector<std::array<int16_t, kBlockSlots>> blocks_;
    mutable std::vector<int16_t> lookup_;
    Stan* stan_ = nullptr;
    const BgLevel* level_ = nullptr;
};

} // namespace Frigate::Level
