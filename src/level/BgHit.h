#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "level/BgLevel.h"
#include "level/Matrix.h"
#include "level/TextureHits.h"

namespace Frigate::Level {

// HitThing: where a ray met a triangle and what it met.
struct HitThing {
    Vec3 hitpos{};
    Vec3 normal{};
    // The display-list command (a Gfx) of the triangle, and which of a G_TRI4's
    // triangles it is (1 to 4; 0 for a G_TRI1): what a shot at a light fixture
    // darkens.
    const void* triCommand = nullptr;
    int16_t triType = 0;
    // The hit triangle's texture number, -1 for none.
    int texture = -1;
    // The hash of the texture's resource path, 0 for none.
    uint64_t textureHash = 0;
};

// The part of the bg code that tests a shot against the rooms' triangles
// (bgBuildRoomVtxBounds, bgTestRayIntersectionInRoom and
// bgTestBulletHitBackground).
class BgHit {
  public:
    explicit BgHit(const BgLevel& level) : level_(level) {
    }

    // The images a triangle's hit is identified by; without them a hit has no
    // texture.
    void SetTextureHits(const TextureHits* textures) {
        textures_ = textures;
    }

    // bgBuildRoomVtxBounds: the bounds of the vertices each vertex load of the
    // room's primary list brings in, moved by the room's position (the
    // whole numbers of it).
    void BuildBounds(int room, const RoomGeometry& geometry);
    // The bounds go with the room's data.
    void DropBounds(int room);
    void Reset();

    // bgTestBulletHitBackground: the nearest triangle of room `room`'s primary
    // list the segment `from`..`to` (world units) meets. `hit` is in room
    // units (the world scaled by the stage's scale).
    bool TestBullet(const Vec3& from, const Vec3& to, int room, HitThing* hit, const RoomGeometry& geometry) const;

    // Whether the room has bounds built (vtx_batch_bounds != NULL).
    bool HasBounds(int room) const;

  private:
    // RoomVtxBatchBounds.
    struct Batch {
        int gdlIndex = 0;
        int32_t min[3] = { 0, 0, 0 };
        int32_t max[3] = { 0, 0, 0 };
    };

    bool TestBatch(const Vec3& from, const Vec3& to, const Vec3& dir, const Batch& batch, int room,
                   const RoomGeometry& geometry, HitThing* hit) const;

    const BgLevel& level_;
    const TextureHits* textures_ = nullptr;
    // The room's batches; a room without bounds has none (bounds_[room] is
    // empty and built_[room] false).
    std::vector<std::vector<Batch>> bounds_;
    std::vector<bool> built_;
};

} // namespace Frigate::Level
