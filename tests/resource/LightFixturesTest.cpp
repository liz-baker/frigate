#include <array>
#include <cstdio>
#include <vector>

#include "gfx/GbiF3dex.h"
#include "level/LightFixtures.h"
#include "ship/utils/StrHash64.h"

using namespace Frigate::Level;
namespace F3dex = Frigate::F3dex;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

Gfx HashPair(uint64_t hash) {
    Gfx pair;
    pair.words.w0 = static_cast<uint32_t>(hash >> 32);
    pair.words.w1 = static_cast<uint32_t>(hash);
    return pair;
}

// A reference to a texture by the hash of its resource path: the command, then
// the hash.
void PushTexture(std::vector<Gfx>& list, const char* path) {
    list.push_back(F3dex::Command(G_SETTIMG_OTR_HASH, 0, 0));
    list.push_back(HashPair(CRC64(path)));
}

// A vertex load of `count` vertices from `vertices`: the resource form, with
// the address in place of the offset.
void PushVertexLoad(std::vector<Gfx>& list, const Vtx* vertices, uint32_t count) {
    list.push_back(
        F3dex::Command(G_VTX_OTR_HASH, (count << 12) | ((count) << 1), reinterpret_cast<uintptr_t>(vertices)));
    list.push_back(HashPair(CRC64("bg/test/room01/vtx/000000")));
}

std::array<Vtx, 4> MakeRoom() {
    std::array<Vtx, 4> vertices{};
    const int16_t positions[4][2] = { { 0, 0 }, { 100, 0 }, { 0, 100 }, { 100, 100 } };
    for (size_t i = 0; i < 4; i++) {
        vertices[i].v.ob[0] = positions[i][0];
        vertices[i].v.ob[1] = positions[i][1];
        vertices[i].v.ob[2] = 0;
        for (uint8_t& channel : vertices[i].v.cn) {
            channel = 0xFF;
        }
    }
    return vertices;
}

// A wall lamp's texture, the lamp's two triangles, then another texture.
std::vector<Gfx> MakeList(const Vtx* vertices) {
    std::vector<Gfx> list;
    PushTexture(list, "textures/WALL_LAMP");
    PushVertexLoad(list, vertices, 4);
    list.push_back(F3dex::Triangle(0, 1, 2));
    list.push_back(F3dex::Triangle(1, 3, 2));
    PushTexture(list, "textures/BRICK");
    list.push_back(F3dex::EndDisplayList());
    return list;
}

void TestLightTexturesAreRecognised() {
    Check(LightFixtures::IsLightTexture(CRC64("textures/WALL_LAMP")), "a wall lamp");
    Check(LightFixtures::IsLightTexture(CRC64("textures/LINEAR_LAMP")), "a linear lamp");
    Check(LightFixtures::IsLightTexture(CRC64("textures/WALL_LAMP.mip")), "a lamp loaded as a mip chain");
    Check(!LightFixtures::IsLightTexture(CRC64("textures/BRICK")), "brick is not a light");
    Check(!LightFixtures::IsLightTexture(CRC64("textures/WALL_LAMP.pal")), "nor is a palette");
}

void TestScanOpensAnEntryPerLightTexture() {
    auto vertices = MakeRoom();
    const std::vector<Gfx> list = MakeList(vertices.data());
    LightFixtures fixtures;
    fixtures.InitTables();
    fixtures.ClearRoom(3);
    fixtures.ScanList(list.data(), list.size());

    const auto& table = fixtures.Fixtures();
    Check(table[0].room == 3 && table[0].start == &list[0] && table[0].end == &list[6],
          "the entry runs from the lamp's texture to the next texture");
    Check(table[1].room == 0, "and there is only the one");

    fixtures.ClearRoom(3);
    Check(fixtures.Fixtures()[0].room == 0, "clearing the room frees its entries");
}

void TestPalettedLampIsOneRequest() {
    std::vector<Gfx> list;
    // Two requests for the same paletted lamp, each a palette then an image.
    for (int i = 0; i < 2; i++) {
        PushTexture(list, "textures/WALL_LAMP.pal");
        PushTexture(list, "textures/WALL_LAMP");
    }
    list.push_back(F3dex::EndDisplayList());
    LightFixtures fixtures;
    fixtures.InitTables();
    fixtures.ClearRoom(2);
    fixtures.ScanList(list.data(), list.size());
    const auto& table = fixtures.Fixtures();
    Check(table[0].room == 2 && table[0].start == &list[0] && table[0].end == &list[4], "the first request's entry");
    Check(table[1].room == 2 && table[1].start == &list[4] && table[1].end == list.data() + list.size(),
          "and the second's");
    Check(table[2].room == 0, "a palette and its image are one request");
}

void TestBreakDarkensTheFixture() {
    auto vertices = MakeRoom();
    const std::vector<Gfx> list = MakeList(vertices.data());
    LightFixtures fixtures;
    fixtures.InitTables();
    fixtures.ClearRoom(3);
    fixtures.ScanList(list.data(), list.size());

    int shards = 0;
    LightFixtures::BreakContext context;
    context.vertices = vertices.data();
    context.vertexCount = vertices.size();
    context.commands = list.data();
    context.commandCount = list.size();
    context.levelScale = 1.0f;
    context.recipLevelScale = 1.0f;
    context.roomOrigin = { 1000.0f, 0.0f, 0.0f };
    Vec3 firstShard{};
    context.createShard = [&](const Vec3& position) {
        if (shards == 0) {
            firstShard = position;
        }
        shards++;
    };

    // The first of the lamp's triangles is shot.
    fixtures.Break(&list[4], 0, 3, context);
    Check(vertices[0].v.cn[0] == 0x3F && vertices[1].v.cn[3] == 0x3F && vertices[2].v.cn[1] == 0x3F,
          "the hit triangle's vertices go to a quarter of their brightness");
    Check(vertices[3].v.cn[0] == 0x3F, "and so does the other triangle's, which shares an edge with it");
    int darkened = 0;
    for (const auto& entry : fixtures.DarkenedVertices()) {
        darkened += entry.room == 3 ? 1 : 0;
    }
    Check(darkened == 4, "four vertices are remembered");
    Check(shards > 10, "glass falls along the hit triangle's edges");
    Check(firstShard[0] >= 1000.0f, "from the room's position");

    // Shooting it again does nothing.
    const int before = shards;
    fixtures.Break(&list[4], 0, 3, context);
    Check(shards == before, "a broken triangle sheds no more glass");

    // A shot in a room with no fixtures does nothing either.
    auto other = MakeRoom();
    fixtures.Break(&list[4], 0, 7, context);
    Check(shards == before, "a shot in another room finds no fixture");
    (void)other;
}

void TestRedarkenAfterAReload() {
    auto vertices = MakeRoom();
    const std::vector<Gfx> list = MakeList(vertices.data());
    LightFixtures fixtures;
    fixtures.InitTables();
    fixtures.ClearRoom(3);
    fixtures.ScanList(list.data(), list.size());

    LightFixtures::BreakContext context;
    context.vertices = vertices.data();
    context.vertexCount = vertices.size();
    context.commands = list.data();
    context.commandCount = list.size();
    fixtures.Break(&list[4], 0, 3, context);

    // The room is read from the file again.
    auto reloaded = MakeRoom();
    fixtures.Redarken(3, reloaded.data(), reloaded.size());
    Check(reloaded[0].v.cn[0] == 0x3F && reloaded[3].v.cn[2] == 0x3F, "the darkening is applied to the fresh vertices");
    auto otherRoom = MakeRoom();
    fixtures.Redarken(4, otherRoom.data(), otherRoom.size());
    Check(otherRoom[0].v.cn[0] == 0xFF, "another room's vertices are not touched");
}

void TestInitForgetsEverything() {
    auto vertices = MakeRoom();
    const std::vector<Gfx> list = MakeList(vertices.data());
    LightFixtures fixtures;
    fixtures.InitTables();
    fixtures.ClearRoom(3);
    fixtures.ScanList(list.data(), list.size());
    LightFixtures::BreakContext context;
    context.vertices = vertices.data();
    context.vertexCount = vertices.size();
    context.commands = list.data();
    context.commandCount = list.size();
    fixtures.Break(&list[4], 0, 3, context);

    fixtures.InitTables();
    Check(fixtures.Fixtures()[0].room == 0 && fixtures.DarkenedVertices()[0].room == 0, "both tables are emptied");
    auto reloaded = MakeRoom();
    fixtures.Redarken(3, reloaded.data(), reloaded.size());
    Check(reloaded[0].v.cn[0] == 0xFF, "and nothing is darkened again");
}

} // namespace

int main() {
    TestLightTexturesAreRecognised();
    TestScanOpensAnEntryPerLightTexture();
    TestPalettedLampIsOneRequest();
    TestBreakDarkensTheFixture();
    TestRedarkenAfterAReload();
    TestInitForgetsEverything();
    return gFailures == 0 ? 0 : 1;
}
