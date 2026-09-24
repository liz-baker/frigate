#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "level/BgHeader.h"
#include "level/StanData.h"

namespace Frigate::Level {

// sub_GAME_7F0B1DDC's result (StanCollisionResult).
enum class StanHit : int32_t {
    None = -2,
    Found = 2,
    TraversalLimit = 5,
};

// The callbacks of the stan locus traversal (sub_GAME_7F0B1DDC). Any may be
// empty.
struct StanLocusCallbacks {
    // Called with each tile as it is visited.
    std::function<void(int tile)> onTile;
    // Called with an edge the circle reaches. Returns true to treat the edge
    // as blocking, without following its link.
    std::function<bool(int tile, int point, float edgeDistance, float distanceA, float distanceB)> onEdge;
    // Called with the tiles visited so far when a blocking edge is found (and,
    // with the whole set, when the traversal finishes clear). Returns true
    // from the first case to carry on past the edge.
    std::function<bool(const int* tiles, int count)> onBlocked;
};

// The stage's stan (collision) file as the game holds it once stanLoadFile has
// run (stan.c). Tiles are indices into the file's tile list; kNoTile stands for
// a null tile pointer.
class Stan {
  public:
    // Rooms the room tables hold (firststaninroom).
    static constexpr int kMaxRooms = 139;

    // The extent of a room's tile points, in stan units.
    struct RoomBounds {
        std::array<int16_t, 3> min{};
        std::array<int16_t, 3> max{};
    };

    // stanLoadFile, with setLevelScale(scale) from load_bg_file: builds the
    // room tables. False, leaving the stan unloaded, when a tile is in a room
    // past kMaxRooms.
    bool Load(std::shared_ptr<const StanData> data, float scale);

    bool Loaded() const {
        return data_ != nullptr;
    }
    int TileCount() const {
        return data_ ? static_cast<int>(data_->tiles.size()) : 0;
    }
    const StanTile& Tile(int tile) const {
        return data_->tiles[static_cast<size_t>(tile)];
    }
    // getTileRoom.
    int TileRoom(int tile) const {
        return Tile(tile).room;
    }
    // dword_CODE_bss_8007B9DC: one past the highest room a tile is in.
    int RoomCount() const {
        return roomCount_;
    }
    // firststaninroom: the room's first tile, or kNoTile.
    int FirstTileInRoom(int room) const {
        return firstInRoom_[static_cast<size_t>(room)];
    }
    // g_StanRoomBounds.
    const RoomBounds& Bounds(int room) const {
        return bounds_[static_cast<size_t>(room)];
    }
    // level_scale.
    float Scale() const {
        return scale_;
    }

    // stanMatchTileName: the tile named `id` (`p1234a2`), or kNoTile.
    int MatchTileName(const std::string& id) const;

    // stanTileHasZeroArea: whether the tile's height-plane triangle has no
    // area seen from above.
    bool TileHasZeroArea(int tile) const;
    // getTileMidPoint, in world units.
    Vec3 TileMidPoint(int tile) const;
    // stanGetPositionYValue: the height of the tile's plane under a world
    // position.
    float PositionY(int tile, float x, float z) const;
    // isPointInsideTriStandTileUnscaled_Maybe: whether a world position is
    // inside the tile's height-plane triangle.
    bool PointInsideTriangle(int tile, float x, float z) const;

    // walkTilesBetweenPoints_NoCallback: follows the segment (in world units)
    // from tile `tile` across tile links. `tile` ends on the last tile
    // reached. False when the segment leaves the tiles or the walk does not
    // end.
    bool WalkTiles(int* tile, float startX, float startZ, float destX, float destZ);

    // sub_GAME_7F0B1DDC: visits the tiles a circle at a world position
    // touches, following links across edges it reaches.
    StanHit Locus(int startTile, float x, float z, float radius, const StanLocusCallbacks& callbacks = {});
    // sub_GAME_7F0B20D0: the locus with no callbacks.
    StanHit TestCircle(int tile, float x, float z, float radius) {
        return Locus(tile, x, z, radius);
    }

    // sub_GAME_7F0AFB78: moves `position` (world units) to the nearest point
    // in a tile's interior a circle of `radius` fits at without touching an
    // edge, and returns that tile (kNoTile if there is none).
    int FindNearestWalkable(Vec3* position, float radius);

    // init_pathtable_something: resolves the tile a pad at `position` stands
    // on, by its name, falling back to the nearest tile that reaches it.
    // Returns 1 when the named tile holds the pad, 2 when another tile was
    // found, and 0 with kNoTile when none was.
    int ResolvePadTile(const Vec3& position, const std::string& tileName, int* tile);

    // stanSavedColl_tile and stanSavedColl_pointI: the tile and edge the last
    // blocked locus or failed walk ended on.
    struct SavedCollision {
        int tile = kNoTile;
        int point = 0;
    };
    const SavedCollision& LastCollision() const {
        return saved_;
    }

  private:
    // getShortest2dDispToInfTileEdge (stan units).
    float EdgeDistance(const StanTile& tile, int edge, float x, float z) const;
    // getShortest2dDispToInfTripleEdge, in stan units.
    float TripleEdgeDistance(const StanTile& tile, int k, float x, float z) const;
    // getPointJustInsideOfTileTriple, in world units.
    Vec3 PointJustInside(int tile, int k) const;

    std::shared_ptr<const StanData> data_;
    float scale_ = 1.0f;
    float inverseScale_ = 1.0f;
    int roomCount_ = 0;
    std::array<int, kMaxRooms> firstInRoom_{};
    std::array<RoomBounds, kMaxRooms> bounds_{};
    SavedCollision saved_;
};

} // namespace Frigate::Level
