#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "level/BgHeader.h"
#include "libultraship/libultra/gbi.h"

namespace Frigate::Level {

// lightfixture.c: the stage's light fixtures, which go dark when shot.
//
// A fixture entry is the run of a room's display list drawn with one light
// texture, not one lamp: several lamps that share a texture share an entry.
// Breaking a light darkens the vertices of the triangle that was hit and of
// every triangle of its run that touches one already darkened, and remembers
// them by (room, vertex) so the darkening survives the room being unloaded and
// read again.
class LightFixtures {
  public:
    static constexpr int kFixtureTableSize = 0x64;
    static constexpr int kDarkenedTableSize = 0x200;

    // A run of a room's display list, as pointers into its commands.
    struct Fixture {
        int16_t room = 0;
        const Gfx* start = nullptr;
        const Gfx* end = nullptr;
    };
    // One darkened vertex, by its index in its room's vertices.
    struct Darkened {
        uint16_t room = 0;
        uint16_t vertex = 0;
    };

    // What Break reads off the stage.
    struct BreakContext {
        // The room's vertices (g_BgRoomInfo[room].vertices).
        Vtx* vertices = nullptr;
        size_t vertexCount = 0;
        // The list the hit command is in, which a triangle's vertex load is
        // found in.
        const Gfx* commands = nullptr;
        size_t commandCount = 0;
        // get_room_data_float1 and get_room_data_float2: the stage's scale and
        // its reciprocal.
        float levelScale = 1.0f;
        float recipLevelScale = 1.0f;
        // getRoomPositionScaledByIndex.
        Vec3 roomOrigin{};
        // glassCreateShard(&position, 0.0, 10.0).
        // TODO(https://github.com/liz-baker/frigate/issues/213): glass shards
        // are not ported; the hook stays empty until they are.
        std::function<void(const Vec3& position)> createShard;
    };

    // lightFixtureInitTables.
    void InitTables();

    // check_if_imageID_is_light, for the hash of a texture's resource path
    // (`textures/<image name>`).
    static bool IsLightTexture(uint64_t textureHash);

    // clear_light_fixturetable_in_room: frees the room's entries and makes it
    // the room that the next entries belong to.
    void ClearRoom(int room);
    // What texLoadFromGdl does with the lights in a list it loads: an entry
    // opens at each light texture and closes at the next texture. The entries
    // belong to the room ClearRoom last named.
    void ScanList(const Gfx* commands, size_t count);
    // lightFixtureEntryBegin and lightFixtureEntryEnd.
    void EntryBegin(const Gfx* dl);
    void EntryEnd(const Gfx* dl);

    // redarken_lights_in_room: darkens again the vertices that were shot out
    // before the room was read.
    void Redarken(int room, Vtx* vertices, size_t vertexCount) const;

    // lightFixtureBreak: darkens the light fixture triangle `hit` (a command
    // in `room`'s list; `triType` 0 for a G_TRI1, 1 to 4 for the triangles of
    // a G_TRI4) belongs to and sheds glass along its edges.
    void Break(const Gfx* hit, uint32_t triType, int room, const BreakContext& context);

    const std::array<Fixture, kFixtureTableSize>& Fixtures() const {
        return fixtures_;
    }
    const std::array<Darkened, kDarkenedTableSize>& DarkenedVertices() const {
        return darkened_;
    }

  private:
    int FindFreeSlot() const;
    bool DarkenedContains(const Vtx* vertex, int room, const BreakContext& context) const;
    void DarkenVertex(Vtx* vertex, int room, const BreakContext& context);
    void DarkenTriangle(const Gfx* gfx, uint32_t triType, int room, const BreakContext& context);
    int DarkenedTriangleVertices(const Gfx* gfx, uint32_t triType, int room, const BreakContext& context) const;
    bool NearDarkened(const int16_t (&coordinate)[3], int room, const BreakContext& context) const;

    std::array<Fixture, kFixtureTableSize> fixtures_{};
    std::array<Darkened, kDarkenedTableSize> darkened_{};
    int currentSlot_ = 0;
    int currentRoom_ = 0;
    int nextDarkened_ = 0;
};

} // namespace Frigate::Level
