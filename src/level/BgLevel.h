#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "level/BgHeader.h"
#include "level/LevelInfo.h"
#include "level/ModelHandle.h"
#include "level/PlayerMove.h"
#include "level/Stan.h"
#include "level/StanData.h"

namespace Frigate::Level {

// An array of `count` items the room owns and the code that edits a loaded
// room reads in place: Gfx commands or Vtx vertices, whose types this header
// does not name. Empty (`count` 0) when the room has none.
struct RoomBlock {
    void* data = nullptr;
    size_t count = 0;

    bool Empty() const {
        return count == 0;
    }
};

// What a loaded room holds.
struct RoomGeometry {
    // Room-local positions of the room's vertices, in blob order.
    std::vector<std::array<int16_t, 3>> vertexPositions;
    // The room's vertices (Vtx), and its primary and secondary display lists
    // (Gfx), which name those vertices by address. They are the room's own
    // copies (the game reads them into the room's memory), so editing them
    // changes this room's next draw and nothing else.
    RoomBlock vertices;
    RoomBlock primaryList;
    RoomBlock secondaryList;
    // Keeps the room's resources alive until it is unloaded.
    std::shared_ptr<void> resident;
};

// What has to happen to a room as it loads, beyond reading it: the fog and
// light fixture work bgLoadRoomModelData does around the file reads.
class RoomObserver {
  public:
    virtual ~RoomObserver() = default;
    // lightFixtureInitTables, at the start of load_bg_file.
    virtual void StageLoading() = 0;
    // The room's vertices and lists are resident (bgLoadRoomModelData after
    // both list loads).
    virtual void RoomLoaded(int room, RoomGeometry& geometry) = 0;
};

// Where a level's data comes from.
class BgSource {
  public:
    virtual ~BgSource() = default;
    // The stage's header region (`bg/<bgName>/header`).
    virtual std::optional<std::vector<uint8_t>> LoadHeader(const LevelEntry& level) = 0;
    // Room `room`'s vertices and lists, already decompressed.
    virtual std::optional<RoomGeometry> LoadRoom(const LevelEntry& level, int room, const BgRoomEntry& entry) = 0;
    // The stage's stan file (`LevelEntry::stanName`); null when it has none.
    virtual std::shared_ptr<const StanData> LoadStan(const LevelEntry& level) {
        (void)level;
        return nullptr;
    }
    // Bond's head motion (the gait model and its animations); null when the
    // source has none, and the player then holds the standing pose.
    virtual std::unique_ptr<HeadMotion> CreateHead() {
        return nullptr;
    }
    // A stage's decompressed setup file, by its name including the country
    // prefix (`UsetupdamZ`).
    virtual std::optional<std::vector<uint8_t>> LoadSetup(const std::string& name) {
        (void)name;
        return std::nullopt;
    }
    // The text of the weapon table (`weapon/stats`); nullopt when the source
    // has none, and the stage then runs without weapons.
    virtual std::optional<std::string> LoadWeaponStats() {
        return std::nullopt;
    }
    // The text of the object model table (`prop/models`); nullopt when the
    // source has none, and the stage then creates no objects.
    virtual std::optional<std::string> LoadPropModels() {
        return std::nullopt;
    }
    // The text of the body and head table (`chr/records`); nullopt when the
    // source has none, and the stage then spawns no characters.
    virtual std::optional<std::string> LoadChrRecords() {
        return std::nullopt;
    }
    // The text of the hat placements (`chr/hats`); nullopt when the source has
    // none, and a hat then sits on a head unadjusted.
    virtual std::optional<std::string> LoadChrHats() {
        return std::nullopt;
    }
    // A text bank's strings, one per slot (an empty string for a slot with no
    // text); `resource` is the bank's resource name (Lang::BankResource).
    // Nullopt when the source has none, and the bank then reads as empty.
    virtual std::optional<std::vector<std::string>> LoadTextBank(const std::string& resource) {
        (void)resource;
        return std::nullopt;
    }
    // Where character, weapon and hat models load from; null when the source
    // has none, and the stage's characters then have no models.
    virtual ModelFactory* Models() {
        return nullptr;
    }
};

// What the game keeps per room (s_room_info).
struct RoomInfo {
    // 0 = unloaded, 1 = loaded and used lately, 2-3 = aging, 4 = unload on the
    // next tick.
    uint8_t modelBinLoaded = 0;
    // Set when something asks for the room (CheckIfRoomModelNeedsLoad); only a
    // room that has not been asked for ages in TickUnload.
    bool requested = false;
    Vec3 minBounds{};
    Vec3 maxBounds{};
    RoomGeometry geometry;
};

// The stage's rooms and portals: load_bg_file's result and the streaming that
// keeps a subset of the rooms resident (bg.c).
class BgLevel {
  public:
    explicit BgLevel(BgSource& source);

    // load_bg_file: reads the header and the stan file, sizes the room table,
    // and works out each room's bounds and each portal's orientation. False if
    // the stage has no usable header or stan file.
    bool Load(LevelId id);

    // cleanup_rooms.
    void Cleanup();

    // bgCheckIfRoomModelNeedsLoad: marks the room in use, loading it if it is
    // not resident. True if it had to be loaded.
    bool CheckIfRoomModelNeedsLoad(int room);
    // What rendering a loaded room does to its age: back to 1.
    void MarkRoomDrawn(int room) {
        rooms_[room].modelBinLoaded = 1;
    }
    // bgLoadRoomModelData.
    void LoadRoomModelData(int room);
    // delete_room_data.
    void DeleteRoomData(int room);
    // unload_rooms.
    void UnloadRooms();
    // bgRoomsTickUnload.
    void TickUnload();

    bool IsLoaded() const {
        return loaded_;
    }
    // Why the last Load() or LoadRoomModelData() failed.
    const std::string& Error() const {
        return error_;
    }
    int MaxNumRooms() const {
        return header_.maxNumRooms;
    }
    const RoomInfo& Room(int room) const {
        return rooms_[room];
    }
    const BgRoomEntry& RoomEntry(int room) const {
        return header_.rooms[room];
    }
    const std::vector<BgPortal>& Portals() const {
        return header_.portals;
    }
    // Portals whose plane's normal is horizontal (D_800443C4).
    bool PortalNormalIsHorizontal(size_t portal) const {
        return portalNormalHorizontal_[portal];
    }
    // sub_GAME_7F0B9F14: whether the segment `from`..`to` passes through the
    // portal's opening. 0 if not, else 1 or 2 by which side of the portal's
    // plane `from` is on. `midDistance` receives D_80044900, the segment
    // midpoint's signed distance from the plane.
    int PortalCrossing(size_t portal, const Vec3& from, const Vec3& to, float* midDistance = nullptr) const;
    // sub_GAME_7F0B9E04: the portal `from`..`to` crosses nearest its plane, -1
    // for none.
    int FindWindowPortal(const Vec3& from, const Vec3& to) const;
    // sub_GAME_7F0BA2D4: adds to `rooms` (`*count` of them, at most
    // `maxCount`) the rooms an enabled portal leads to from a room already in
    // it, where the portal's box overlaps the box `boxMin`..`boxMax` (world
    // units).
    void AddRoomsThroughPortals(const Vec3& boxMin, const Vec3& boxMax, int* rooms, int* count, int maxCount) const;
    // bgToggleDataPortalsContrlBytes1Bit1: a door opening or closing over a
    // portal disables or re-enables it, which controls both room visibility
    // and (via CdType::ClosedDoors and friends) whether a stan test crosses it.
    void SetPortalDisabled(size_t portal, bool disabled) {
        if (portal >= header_.portals.size()) {
            return;
        }
        if (disabled) {
            header_.portals[portal].controlBytes1 |= kPortalDisabled;
        } else {
            header_.portals[portal].controlBytes1 &= static_cast<uint8_t>(~kPortalDisabled);
        }
    }
    const BgHeader& Header() const {
        return header_;
    }
    const LevelEntry& Level() const {
        return *level_;
    }
    // The stage's collision tiles, loaded with the header.
    Stan& StanFile() {
        return stan_;
    }
    const Stan& StanFile() const {
        return stan_;
    }
    size_t LevelIndex() const {
        return levelIndex_;
    }
    float VisibilityScale() const {
        return visibilityScale_;
    }
    // Who is told as the stage and its rooms load (null for none).
    void SetObserver(RoomObserver* observer) {
        observer_ = observer;
    }
    // g_RoomLoadBudget: how many rooms may load this frame.
    int RoomLoadBudget() const {
        return roomLoadBudget_;
    }
    void SetRoomLoadBudget(int budget) {
        roomLoadBudget_ = budget;
    }

  private:
    void CalcRoomBounds(int room);
    void ExtendRoomBoundsByPortals(int room);
    void OrderPortal(size_t portal);
    void MarkSpecialPortals();

    BgSource& source_;
    RoomObserver* observer_ = nullptr;
    const LevelEntry* level_ = nullptr;
    size_t levelIndex_ = 0;
    bool loaded_ = false;
    std::string error_;
    BgHeader header_;
    Stan stan_;
    std::vector<RoomInfo> rooms_;
    std::vector<bool> portalNormalHorizontal_;
    float visibilityScale_ = 1.0f;
    int roomLoadBudget_ = 0;
};

} // namespace Frigate::Level
