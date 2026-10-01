#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "level/Matrix.h"

namespace Frigate::Level {

// A vertex of an effect's quad, as the game keeps it (a Vtx without the flag).
struct EffectVertex {
    std::array<int16_t, 3> position{};
    std::array<int16_t, 2> texture{};
    std::array<uint8_t, 4> colour{};
};

// The size of an image of the global image table, by its offset there
// (`hud/images`).
struct EffectImageSize {
    int width = 0;
    int height = 0;
};

// The global image table's entries the effects draw (docs/formats/
// goldeneye-hud-images.md), with the sizes `hud/images` gives.
namespace EffectImage {
constexpr int kGeneric = 0xAC8;
constexpr int kImpacts = 0xAD4;
constexpr int kSmoke = 0xBC4;
constexpr int kScattered = 0xC0C;
constexpr int kFlare1 = 0xC48;
constexpr int kFlare2 = 0xC54;
constexpr int kFlare3 = 0xC60;
constexpr int kFlare4 = 0xC6C;
constexpr int kFlare5 = 0xC78;
constexpr int kGlassOverlay = 0xD44;
constexpr int kEntrySize = 12;
} // namespace EffectImage

class EffectImages {
  public:
    // Reads the table's text; false for text it can't read.
    bool Parse(const std::string& text);
    // The image at `offset`; zero-sized if the table lacks it.
    EffectImageSize Size(int offset) const;

  private:
    std::unordered_map<int, EffectImageSize> sizes_;
};

// s_impacttype.
struct ImpactType {
    float width;
    float height;
    uint8_t appearance;
    uint8_t unk1;
    uint8_t unk2;
};
constexpr int kImpactTypeCount = 20;
const ImpactType& ImpactTypeOf(int type);

// PROPSTATE_DAMAGED and PROPSTATE_2: the bits of an object's state that say it
// has a mark to draw in the opaque (0x1) or the blended (0x2) pass.
constexpr uint8_t kStateMarkOpaque = 0x01;
constexpr uint8_t kStateMarkBlended = 0x02;

// BulletImpact: a bullet hole or scorch of a hit, on a wall (in a room) or on a
// prop.
struct BulletImpact {
    // room: negative for a free slot.
    int16_t room = -1;
    int16_t type = 0;
    // The prop the mark is on (-1 for none) and the matrix of its model it is
    // placed by; and the flag that clears it with the prop's door or glass.
    int prop = -1;
    int matrixIndex = 0;
    int8_t clearFlag = 0;
    std::array<EffectVertex, 4> vertices{};
};

// What placing a mark needs of where it goes (explosionCreateBulletImpact).
struct ImpactPlacement {
    Vec3 position{};
    Vec3 normal{};
    int type = 0;
    int room = 0;
    // The prop the mark is on and its model's matrix for the node hit; none
    // places the mark on the room.
    int prop = -1;
    const Mtxf* propMatrix = nullptr;
    int matrixIndex = 0;
    int8_t clearFlag = 0;
    // cheatIsActive(CHEAT_PAINTBALL).
    bool paintball = false;
    // get_room_data_float1 and getRoomPositionByIndex.
    float levelScale = 1.0f;
    Vec3 roomPosition{};
};

// The marks shots leave (g_BulletImpactBuffer) and the scorches explosions
// leave (g_ScorchBuffer).
class BulletImpacts {
  public:
    static constexpr int kMaxImpacts = 100;
    static constexpr int kMaxScorches = 20;
    using RandomFn = std::function<uint32_t()>;

    // A burn mark on the floor (struct Scorch).
    struct Scorch {
        int16_t room = -1;
        std::array<EffectVertex, 4> vertices{};
    };

    // alloc_explosion_smoke_casing_scorch_impact_buffers' share.
    void Reset();
    void SetImages(const EffectImages* images) {
        images_ = images;
    }

    // explosionCreateBulletImpact. Returns the bit of the prop's state that
    // says the mark is there to draw (kStateMark*), 0 when the mark is on a room.
    uint8_t Create(const ImpactPlacement& placement, const RandomFn& random);
    // explosionClearBulletImpactRoomByFlag.
    void ClearByFlag(int prop, int8_t flag);
    // explosionClearBulletImpactRoom.
    void ClearForProp(int prop);

    // explosionScorchTick: a burn of `size` at `position` in `room`.
    void CreateScorch(Vec3 position, float size, int room, const Vec3& roomPosition, float levelScale,
                      const RandomFn& random);

    const std::array<BulletImpact, kMaxImpacts>& Impacts() const {
        return impacts_;
    }
    const std::array<Scorch, kMaxScorches>& Scorches() const {
        return scorches_;
    }

  private:
    // explosionSetBulletImpactAlpha.
    void SetAlpha(int start);

    std::array<BulletImpact, kMaxImpacts> impacts_;
    std::array<Scorch, kMaxScorches> scorches_;
    // g_NumImpactEntries and g_NumScorchEntries.
    int nextImpact_ = 0;
    int nextScorch_ = 0;
    const EffectImages* images_ = nullptr;
};

} // namespace Frigate::Level
