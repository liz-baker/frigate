#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "level/BgHeader.h"
#include "level/StanData.h"
#include "level/StanProps.h"

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

// g_StanTileSpecialFlags' bits.
constexpr uint8_t kStanTileForceCrouch = 0x02;
constexpr uint8_t kStanTileLadder = 0x40;

// What stanTileDistanceRelated learns about the tiles a circle touches.
struct StanTileProbe {
    StanHit hit = StanHit::None;
    // stanGetLocusField0: the circle is on, or would enter, a force-crouch tile.
    bool forceCrouch = false;
    // stanGetLocusCount: a ladder tile borders a tile the circle touches.
    bool ladder = false;
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

    // The props the collision tests consider; none until set.
    void SetProps(const StanProps* props) {
        props_ = props;
    }

    // stanFindTileBelowPos: the highest tile beneath a world position, among
    // the tiles of `rooms` (a 0xFF-terminated list of at most four) or of every
    // room when null. `*y` is that tile's height when one is found. kNoTile
    // when none is.
    int FindTileBelow(const Vec3& position, const uint8_t* rooms, float* y);

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

    // stanSavedColl_*: the last blocked locus, failed walk or failed line test.
    struct SavedCollision {
        // The tile and edge, or kNoTile.
        int tile = kNoTile;
        int point = 0;
        // D_800413BC: pointA and pointB hold an edge (world x, z).
        bool hit = false;
        std::array<float, 2> pointA{};
        std::array<float, 2> pointB{};
        // stanSavedColl_someMin: how far along the tested line the hit was.
        float fraction = 1.0f;
        // stanSavedColl_posData: the prop slot whose polygon was hit, -1 for none.
        int prop = -1;
    };
    const SavedCollision& LastCollision() const {
        return saved_;
    }
    // stanResetHits.
    void ResetHits() {
        saved_.tile = kNoTile;
        saved_.point = 0;
        saved_.hit = false;
    }
    // getCollisionEdge_maybe: the edge of the last collision, in world units
    // with y 0 for a prop's. False when there was none.
    bool CollisionEdge(Vec3* a, Vec3* b) const;

    // stanTestPointWithinTileBoundsMaybe: whether a world position is within two
    // stan units of the inside of all three of the tile's height-plane edges.
    bool PointWithinTileBounds(int tile, float x, float z) const;

    // walkTilesBetweenPoints_NotingRooms: WalkTiles that also records each new
    // room the walk enters, up to `maxRooms`.
    bool WalkTilesNotingRooms(int* tile, float startX, float startZ, float destX, float destZ, int* rooms,
                              int* roomCount, int maxRooms);

    // sub_GAME_7F0B21B0: the locus that collects the rooms the circle reaches.
    // Found when it touches two or more edges.
    StanHit TestCircleRooms(int tile, float x, float z, float radius, int* rooms, int* roomCount, int maxRooms);
    // stanTileDistanceRelated. Also remembers a bordering ladder tile.
    StanTileProbe TileDistance(int tile, float x, float z, float radius);
    // stanTestLocusEdgeAboveY: the locus, blocked by an edge whose points are
    // both above `y` (world units).
    StanHit TestLocusEdgeAboveY(int tile, float x, float z, float radius, float y);

    // stanTestLineUnobstructed: walks the segment (world units) from `*tile`
    // and reports whether the tiles reach its end (non-zero). `*tile` ends on
    // the last tile reached; on a hit the edge and how far along it was are
    // saved. `height`, `above`, `low` and `high` only matter to props.
    int TestLineUnobstructed(int* tile, float x, float z, float destX, float destZ, int cdTypes, float height,
                             float above, float low, float high);
    // stanTestVolume: circle test; negative (None) when nothing is hit.
    int TestVolume(int* tile, float x, float z, float radius, int cdTypes, float height, float above);

    // stanGetMoveBondCollisionTiles: for the ladder tile TileDistance last saw,
    // finds the neighbouring ladder tile and the four points spanning both
    // (world units). False when it has no ladder neighbour.
    bool GetLadderCollision(int* tile1, int* tile2, std::array<Vec3, 4>* points) const;

  private:
    bool WalkTilesImpl(int* tile, float startX, float startZ, float destX, float destZ,
                       const std::function<void(int, int)>& callback);
    // stanGetTileOrderedPointWorldPos.
    Vec3 OrderedPointWorld(int tile, int k) const;
    // getTileEdgePoints, in world units.
    void EdgePoints(int tile, int point, Vec3* from, Vec3* to) const;

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
    const StanProps* props_ = nullptr;
    // dword_CODE_bss_8007BA0C: the ladder tile TileDistance last found.
    int ladderTile_ = kNoTile;
};

} // namespace Frigate::Level
