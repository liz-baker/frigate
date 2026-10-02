#include "level/LightFixtures.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>

#include "level/TriangleList.h"
#include "ship/utils/StrHash64.h"

namespace Frigate::Level {

namespace {

// check_if_imageID_is_light's images, by resource path: the texture and its
// palette.
constexpr std::array<const char*, 10> kLightTextures = {
    "WALL_LAMP", "203_LIGHT", "205_LIGHT",    "252_LIGHT", "PANEL_LAMP",
    "255_LIGHT", "256_LIGHT", "HANGING_LAMP", "NEON_LAMP", "LINEAR_LAMP",
};

struct LightHashes {
    std::array<uint64_t, kLightTextures.size()> images;
    std::array<uint64_t, kLightTextures.size()> palettes;
};

const LightHashes& Hashes() {
    static const LightHashes hashes = [] {
        LightHashes out{};
        for (size_t i = 0; i < kLightTextures.size(); i++) {
            const std::string path = std::string("textures/") + kLightTextures[i];
            out.images[i] = CRC64(path.c_str());
            out.palettes[i] = CRC64((path + ".pal").c_str());
        }
        return out;
    }();
    return hashes;
}

// The light texture a hash names (its image or its palette), else -1.
// `palette` is set when it names the palette.
int LightTextureOf(uint64_t hash, bool* palette) {
    const LightHashes& hashes = Hashes();
    for (size_t i = 0; i < kLightTextures.size(); i++) {
        if (hashes.images[i] == hash || hashes.palettes[i] == hash) {
            *palette = hashes.palettes[i] == hash;
            return static_cast<int>(i);
        }
    }
    *palette = false;
    return -1;
}

// lightFindVertexBaseForTri: the vertices the last vertex load before `gfx`
// names. A load names its vertices by address, or by offset into the room's.
Vtx* VertexBase(const Gfx* gfx, const LightFixtures::BreakContext& context) {
    const Gfx* load = nullptr;
    size_t i = 0;
    while (i < context.commandCount && context.commands + i < gfx) {
        const uint8_t opcode = Opcode(context.commands[i]);
        if (opcode == static_cast<uint8_t>(G_VTX_OTR_HASH)) {
            load = &context.commands[i];
        }
        i += F3dex::IsExpandedCommand(opcode) ? 2 : 1;
    }
    if (load == nullptr) {
        return nullptr;
    }
    const uintptr_t operand = load->words.w1;
    if (operand > 0xFFFFF) {
        return reinterpret_cast<Vtx*>(operand);
    }
    return reinterpret_cast<Vtx*>(reinterpret_cast<char*>(context.vertices) + operand);
}

} // namespace

void LightFixtures::InitTables() {
    for (Fixture& fixture : fixtures_) {
        fixture.room = 0;
    }
    for (Darkened& darkened : darkened_) {
        darkened.room = 0;
    }
    nextDarkened_ = 0;
}

bool LightFixtures::IsLightTexture(uint64_t textureHash) {
    const LightHashes& hashes = Hashes();
    for (size_t i = 0; i < kLightTextures.size(); i++) {
        if (hashes.images[i] == textureHash) {
            return true;
        }
    }
    return false;
}

int LightFixtures::FindFreeSlot() const {
    for (int i = 0; i != kFixtureTableSize; i++) {
        if (fixtures_[static_cast<size_t>(i)].room == 0) {
            return i;
        }
    }
    return kFixtureTableSize;
}

void LightFixtures::EntryBegin(const Gfx* dl) {
    currentSlot_ = FindFreeSlot();
    if (currentSlot_ != kFixtureTableSize) {
        Fixture& fixture = fixtures_[static_cast<size_t>(currentSlot_)];
        fixture.room = static_cast<int16_t>(currentRoom_);
        fixture.start = dl;
    }
}

void LightFixtures::EntryEnd(const Gfx* dl) {
    if (currentSlot_ != kFixtureTableSize) {
        fixtures_[static_cast<size_t>(currentSlot_)].end = dl;
    }
}

void LightFixtures::ClearRoom(int room) {
    for (Fixture& fixture : fixtures_) {
        if (room == fixture.room) {
            fixture.room = 0;
        }
    }
    currentRoom_ = room;
}

void LightFixtures::ScanList(const Gfx* commands, size_t count) {
    // The texture whose run is open, as an index into the light textures, and
    // whether the last reference was its palette (the image it is for follows
    // in the same request).
    int open = -1;
    bool afterPalette = false;
    for (size_t i = 0; i < count; i += F3dex::IsExpandedCommand(Opcode(commands[i])) ? 2 : 1) {
        if (Opcode(commands[i]) != static_cast<uint8_t>(G_SETTIMG_OTR_HASH) || i + 1 >= count) {
            continue;
        }
        bool palette;
        const int texture = LightTextureOf(OperandHash(commands[i + 1]), &palette);
        const bool sameRequest = texture >= 0 && texture == open && afterPalette && !palette;
        afterPalette = palette;
        if (sameRequest) {
            continue;
        }
        // A new texture request: it closes the run before it.
        if (open >= 0) {
            EntryEnd(&commands[i]);
        }
        if (texture >= 0) {
            EntryBegin(&commands[i]);
        }
        open = texture;
    }
    if (open >= 0) {
        EntryEnd(commands + count);
    }
}

void LightFixtures::Redarken(int room, Vtx* vertices, size_t vertexCount) const {
    for (const Darkened& darkened : darkened_) {
        if (room != darkened.room || darkened.vertex >= vertexCount) {
            continue;
        }
        for (uint8_t& channel : vertices[darkened.vertex].v.cn) {
            channel >>= 2;
        }
    }
}

bool LightFixtures::DarkenedContains(const Vtx* vertex, int room, const BreakContext& context) const {
    const auto index = static_cast<int>(vertex - context.vertices);
    for (const Darkened& darkened : darkened_) {
        if (room == darkened.room && index == darkened.vertex) {
            return true;
        }
    }
    return false;
}

void LightFixtures::DarkenVertex(Vtx* vertex, int room, const BreakContext& context) {
    if (DarkenedContains(vertex, room, context)) {
        return;
    }

    Darkened& entry = darkened_[static_cast<size_t>(nextDarkened_)];
    entry.room = static_cast<uint16_t>(room);
    entry.vertex = static_cast<uint16_t>(vertex - context.vertices);

    for (uint8_t& channel : vertex->v.cn) {
        channel >>= 2;
    }

    // A circular table: on a stage with more broken lights than it holds, the
    // oldest come back.
    nextDarkened_++;
    if (nextDarkened_ >= kDarkenedTableSize) {
        nextDarkened_ = 0;
    }
}

void LightFixtures::DarkenTriangle(const Gfx* gfx, uint32_t triType, int room, const BreakContext& context) {
    int slot[3];
    TriangleSlots(gfx, triType, slot);
    Vtx* vertices = VertexBase(gfx, context);
    if (vertices == nullptr) {
        return;
    }
    DarkenVertex(&vertices[slot[0]], room, context);
    DarkenVertex(&vertices[slot[1]], room, context);
    DarkenVertex(&vertices[slot[2]], room, context);
}

int LightFixtures::DarkenedTriangleVertices(const Gfx* gfx, uint32_t triType, int room,
                                            const BreakContext& context) const {
    int slot[3];
    TriangleSlots(gfx, triType, slot);
    Vtx* vertices = VertexBase(gfx, context);
    if (vertices == nullptr) {
        return 0;
    }
    return static_cast<int>(DarkenedContains(&vertices[slot[1]], room, context)) +
           static_cast<int>(DarkenedContains(&vertices[slot[0]], room, context)) +
           static_cast<int>(DarkenedContains(&vertices[slot[2]], room, context));
}

bool LightFixtures::NearDarkened(const int16_t (&coordinate)[3], int room, const BreakContext& context) const {
    for (const Darkened& darkened : darkened_) {
        if (room != darkened.room) {
            continue;
        }
        const Vtx& vertex = context.vertices[darkened.vertex];
        const int dx = std::abs(vertex.v.ob[0] - coordinate[0]);
        const int dy = std::abs(vertex.v.ob[1] - coordinate[1]);
        const int dz = std::abs(vertex.v.ob[2] - coordinate[2]);
        if ((dx + dy + dz) < static_cast<int>(context.levelScale * 100.0f)) {
            return true;
        }
    }
    return false;
}

void LightFixtures::Break(const Gfx* hit, uint32_t triType, int room, const BreakContext& context) {
    for (const Fixture& fixture : fixtures_) {
        // A stale entry of an unloaded room is rejected here, before its
        // pointers are compared.
        if (room != fixture.room) {
            continue;
        }
        if (hit < fixture.start || hit >= fixture.end) {
            continue;
        }

        if (DarkenedTriangleVertices(hit, triType, fixture.room, context) != 0) {
            return;
        }
        DarkenTriangle(hit, triType, fixture.room, context);

        // The hit triangle's corners and edges, as room-local shorts.
        int slot[3];
        TriangleSlots(hit, triType, slot);
        const Vtx* base = VertexBase(hit, context);
        if (base == nullptr) {
            return;
        }
        int16_t corner[3][3];
        for (int i = 0; i < 3; i++) {
            for (int axis = 0; axis < 3; axis++) {
                corner[i][axis] = base[slot[i]].v.ob[axis];
            }
        }
        int16_t diff12[3];
        int16_t diff13[3];
        int16_t diff23[3];
        for (int axis = 0; axis < 3; axis++) {
            diff12[axis] = static_cast<int16_t>(corner[0][axis] - corner[1][axis]);
            diff13[axis] = static_cast<int16_t>(corner[0][axis] - corner[2][axis]);
            diff23[axis] = static_cast<int16_t>(corner[1][axis] - corner[2][axis]);
        }

        // One shard every 10 world units along each edge.
        const auto step = [&](const int16_t(&diff)[3]) {
            const float length =
                std::sqrt(static_cast<float>((diff[0] * diff[0]) + (diff[1] * diff[1]) + (diff[2] * diff[2])));
            return 10.0f / (context.recipLevelScale * length);
        };
        const float step12 = step(diff12);
        const float step13 = step(diff13);
        const float step23 = step(diff23);

        const auto shedShards = [&](const int16_t(&from)[3], const int16_t(&diff)[3], float stepSize) {
            for (float fraction = 0.0f; fraction < 1.0f; fraction += stepSize) {
                const Vec3 position = {
                    ((from[0] + (diff[0] * fraction)) * context.recipLevelScale) + context.roomOrigin[0],
                    ((from[1] + (diff[1] * fraction)) * context.recipLevelScale) + context.roomOrigin[1],
                    ((from[2] + (diff[2] * fraction)) * context.recipLevelScale) + context.roomOrigin[2],
                };
                if (context.createShard) {
                    context.createShard(position);
                }
            }
        };
        shedShards(corner[1], diff12, step12);
        shedShards(corner[2], diff13, step13);
        shedShards(corner[2], diff23, step23);

        // Every triangle of the run that touches a darkened vertex goes dark
        // with it, so the whole fixture does.
        for (const Gfx* gfx = fixture.start; gfx < fixture.end; gfx += F3dex::IsExpandedCommand(Opcode(*gfx)) ? 2 : 1) {
            const uint8_t opcode = Opcode(*gfx);
            uint32_t first;
            uint32_t last;
            if (opcode == static_cast<uint8_t>(Fast::F3DEX_G_TRI1)) {
                first = 0;
                last = 0;
            } else if (opcode == static_cast<uint8_t>(Fast::INDY_G_TRI4)) {
                first = 1;
                last = 4;
            } else {
                continue;
            }
            for (uint32_t type = first; type <= last; type++) {
                int candidate[3];
                TriangleSlots(gfx, type, candidate);
                const Vtx* vertices = VertexBase(gfx, context);
                if (vertices == nullptr) {
                    break;
                }
                bool darken = false;
                for (int i = 0; i < 3 && !darken; i++) {
                    darken = NearDarkened(vertices[candidate[i]].v.ob, fixture.room, context);
                }
                if (darken) {
                    DarkenTriangle(gfx, type, fixture.room, context);
                }
            }
        }
        return;
    }
}

} // namespace Frigate::Level
