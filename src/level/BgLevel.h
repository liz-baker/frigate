#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "level/BgHeader.h"
#include "level/LevelInfo.h"

namespace Frigate::Level {

// What a loaded room holds.
struct RoomGeometry {
    // Room-local positions of the room's vertices, in blob order.
    std::vector<std::array<int16_t, 3>> vertexPositions;
    // Resource paths of the room's display lists; empty when it has none.
    std::string primaryList;
    std::string secondaryList;
    // Keeps the room's resources alive until it is unloaded.
    std::shared_ptr<void> resident;
};

// Where a level's data comes from.
class BgSource {
  public:
    virtual ~BgSource() = default;
    // The stage's header region (`bg/<bgName>/header`).
    virtual std::optional<std::vector<uint8_t>> LoadHeader(const LevelEntry& level) = 0;
    // Room `room`'s vertices and lists, already decompressed.
    virtual std::optional<RoomGeometry> LoadRoom(const LevelEntry& level, int room, const BgRoomEntry& entry) = 0;
    // A stage's decompressed setup file, by its name including the country
    // prefix (`UsetupdamZ`).
    virtual std::optional<std::vector<uint8_t>> LoadSetup(const std::string& name) {
        (void)name;
        return std::nullopt;
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

    // load_bg_file: reads the header, sizes the room table, and works out each
    // room's bounds and each portal's orientation. False if the stage has no
    // usable header.
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
    const BgHeader& Header() const {
        return header_;
    }
    const LevelEntry& Level() const {
        return *level_;
    }
    size_t LevelIndex() const {
        return levelIndex_;
    }
    float VisibilityScale() const {
        return visibilityScale_;
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
    const LevelEntry* level_ = nullptr;
    size_t levelIndex_ = 0;
    bool loaded_ = false;
    std::string error_;
    BgHeader header_;
    std::vector<RoomInfo> rooms_;
    std::vector<bool> portalNormalHorizontal_;
    float visibilityScale_ = 1.0f;
    int roomLoadBudget_ = 0;
};

} // namespace Frigate::Level
