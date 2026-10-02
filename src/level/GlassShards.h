#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "level/Matrix.h"

namespace Frigate::Level {

// s_shattered_window_piece: a triangle of broken glass flying away.
struct GlassShard {
    // active: 0 for a free slot, else the ticks it has flown.
    int32_t active = 0;
    Vec3 position{};
    Vec3 rotation{};
    Vec3 velocity{};
    Vec3 angularVelocity{};
    // The triangle's three corners: position, texture coordinates and colour.
    struct Corner {
        std::array<int16_t, 3> position{};
        std::array<int16_t, 2> texture{};
        std::array<uint8_t, 4> colour{};
    };
    std::array<Corner, 3> corners{};
};

// The broken glass (glass.c).
class GlassShards {
  public:
    using RandomFn = std::function<uint32_t()>;

    // alloc_shattered_window_pieces: `capacity` is 200 split between the
    // players, halved on the Streets and Depot.
    void Allocate(int capacity);
    // cleanup_window_pieces.
    void Clear();

    // glassCreateShard: a shard of size `size` at `position`, turned `rotX`
    // about X.
    void Create(const Vec3& position, float rotX, float size, const RandomFn& random);

    // sub_GAME_7F0A1DA0: a window of the box `xmin`..`xmax`, `ymin`..`ymax` in
    // the plane the axes give, at `position`, breaks into shards.
    void ShatterWindow(const Vec3& position, const Vec3& xAxis, const Vec3& yAxis, const Vec3& zAxis, float xmin,
                       float xmax, float ymin, float ymax, const RandomFn& random);

    // update_broken_windows.
    void Update(int32_t clockTimer);

    const std::vector<GlassShard>& Shards() const {
        return shards_;
    }

  private:
    std::vector<GlassShard> shards_;
    // g_NextShardNum.
    size_t next_ = 0;
};

} // namespace Frigate::Level
