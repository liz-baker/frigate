#include "resource/factory/AnimationClipFactory.h"
#include "resource/factory/FontFactory.h"
#include "resource/factory/ModelSkeletonFactory.h"
#include "resource/factory/ModelTreeFactory.h"
#include "resource/factory/StanFactory.h"
#include "resource/factory/TextFactory.h"
#include "resource/type/AnimationClip.h"
#include "resource/type/Font.h"
#include "resource/type/ModelSkeleton.h"
#include "resource/type/ModelTree.h"
#include "resource/type/Stan.h"
#include "resource/type/Text.h"

#include "ship/resource/File.h"
#include "ship/resource/Resource.h"
#include "ship/utils/binarytools/BinaryReader.h"
#include "ship/utils/binarytools/MemoryStream.h"
#include "ship/utils/binarytools/endianness.h"

#include <cstdio>
#include <cstring>
#include <vector>

// Desktop-testable correctness checks for frigate's Text/Font resource
// factories (frigate issue #11), against synthetic big-endian buffers built
// from the exact on-disk layouts documented in
// docs/formats/goldeneye-font-text.md -- no GoldenEye ROM data is needed
// or used, since none may be redistributed (see README.md#legal), same
// rationale as tests/audio/AudioUcodeTest.cpp.

using namespace Frigate;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

void AppendBEU32(std::vector<char>& buffer, uint32_t value) {
    buffer.push_back(static_cast<char>((value >> 24) & 0xFF));
    buffer.push_back(static_cast<char>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<char>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<char>(value & 0xFF));
}

void AppendBEI32(std::vector<char>& buffer, int32_t value) {
    AppendBEU32(buffer, static_cast<uint32_t>(value));
}

void AppendBEU16(std::vector<char>& buffer, uint16_t value) {
    buffer.push_back(static_cast<char>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<char>(value & 0xFF));
}

std::shared_ptr<Ship::ResourceInitData> MakeInitData() {
    auto initData = std::make_shared<Ship::ResourceInitData>();
    initData->Format = RESOURCE_FORMAT_BINARY;
    initData->ByteOrder = Ship::Endianness::Big;
    initData->Type = 0;
    initData->ResourceVersion = 0;
    initData->IsCustom = false;
    initData->Id = 0;
    return initData;
}

std::shared_ptr<Ship::File> MakeFile(const std::vector<char>& body,
                                     const std::shared_ptr<Ship::ResourceInitData>& initData) {
    auto file = std::make_shared<Ship::File>();
    file->Buffer = std::make_shared<std::vector<char>>(body);
    file->IsLoaded = true;
    auto stream = std::make_shared<Ship::MemoryStream>(file->Buffer);
    auto reader = std::make_shared<Ship::BinaryReader>(stream);
    reader->SetEndianness(initData->ByteOrder);
    file->Reader = reader;
    return file;
}

std::vector<char> BuildFontBody(int32_t width, int32_t height, uint32_t pixelOffset, size_t pixelBytes) {
    std::vector<char> body;
    for (size_t i = 0; i < Font::kKerningTableSize; i++) {
        AppendBEI32(body, static_cast<int32_t>(i));
    }
    for (size_t i = 0; i < Font::kNumChars; i++) {
        AppendBEI32(body, static_cast<int32_t>(i)); // index
        AppendBEI32(body, 10);                      // baseline
        AppendBEI32(body, height);
        AppendBEI32(body, width);
        AppendBEI32(body, -1); // kerningIndex
        AppendBEU32(body, pixelOffset);
    }
    for (size_t i = 0; i < pixelBytes; i++) {
        body.push_back(static_cast<char>(i));
    }
    return body;
}

void TestFontFactoryParsesKerningCharsAndPixels() {
    // Width 5 pads to an 8-byte row stride; every glyph reads the same 3 rows.
    const size_t headerBytes = (Font::kKerningTableSize + Font::kNumChars * 6) * 4;
    auto body = BuildFontBody(5, 3, static_cast<uint32_t>(headerBytes), 24);

    auto initData = MakeInitData();
    auto file = MakeFile(body, initData);

    ResourceFactoryBinaryFontV0 factory;
    auto resource = factory.ReadResource(file, initData);
    Check(resource != nullptr, "Font factory returns a resource");

    auto font = std::static_pointer_cast<Font>(resource);
    Check(font->Kerning[0] == 0 && font->Kerning[168] == 168, "Kerning table decoded in order");
    Check(font->Chars.size() == Font::kNumChars, "All chars parsed");
    Check(font->Chars[0].width == 5 && font->Chars[0].kerningIndex == -1, "Char fields decoded correctly");
    Check(font->Chars[93].index == 93, "Last char's index decoded correctly");
    Check(font->Chars[0].Pixels.size() == 24, "Glyph pixels span stride * height bytes");
    Check(font->Chars[93].Pixels[0] == 0 && font->Chars[93].Pixels[23] == 23, "Glyph pixels copied byte-for-byte");
}

void TestFontFactoryRejectsGlyphPastEndOfSegment() {
    const size_t headerBytes = (Font::kKerningTableSize + Font::kNumChars * 6) * 4;
    auto body = BuildFontBody(8, 4, static_cast<uint32_t>(headerBytes), 31); // needs 32

    auto initData = MakeInitData();
    auto file = MakeFile(body, initData);

    ResourceFactoryBinaryFontV0 factory;
    Check(factory.ReadResource(file, initData) == nullptr, "Glyph running past the segment is rejected");
}

void TestTextFactoryResolvesOffsetsToStrings() {
    // Build: 1024 offset slots, only slot 0 and slot 5 populated, followed
    // by the two NUL-terminated strings they point to.
    std::vector<char> body;
    size_t offsetTableBytes = Text::kOffsetTableSize * sizeof(uint32_t);

    std::vector<uint32_t> offsets(Text::kOffsetTableSize, 0);
    offsets[0] = static_cast<uint32_t>(offsetTableBytes);
    offsets[5] = static_cast<uint32_t>(offsetTableBytes + 6); // after "Hello\0"

    for (uint32_t offset : offsets) {
        AppendBEU32(body, offset);
    }

    const char* first = "Hello";
    const char* second = "Bond";
    body.insert(body.end(), first, first + std::strlen(first) + 1);
    body.insert(body.end(), second, second + std::strlen(second) + 1);

    auto initData = MakeInitData();
    auto file = MakeFile(body, initData);

    ResourceFactoryBinaryTextV0 factory;
    auto resource = factory.ReadResource(file, initData);
    Check(resource != nullptr, "Text factory returns a resource");

    auto text = std::static_pointer_cast<Text>(resource);
    Check(text->Strings.size() == Text::kOffsetTableSize, "Strings vector matches the fixed offset-table size");
    Check(text->Strings[0] == "Hello", "Slot 0 resolved to the right string");
    Check(text->Strings[5] == "Bond", "Slot 5 resolved to the right string");
    Check(text->Strings[1].empty(), "A zero offset slot stays empty");
}

void TestModelSkeletonFactoryParsesJoints() {
    std::vector<char> body;
    AppendBEU32(body, 2); // joint count
    AppendBEU16(body, 0x401);
    AppendBEU16(body, 0x00);
    AppendBEU16(body, 0x00);
    AppendBEU16(body, 0x02);
    AppendBEU16(body, 0x0F);
    AppendBEU16(body, 0x12);

    auto initData = MakeInitData();
    auto file = MakeFile(body, initData);

    ResourceFactoryBinaryModelSkeletonV0 factory;
    auto resource = factory.ReadResource(file, initData);
    Check(resource != nullptr, "ModelSkeleton factory returns a resource");

    auto skeleton = std::static_pointer_cast<ModelSkeleton>(resource);
    Check(skeleton->Joints.size() == 2, "Joint count decoded correctly");
    Check(skeleton->Joints[0].nodeType == 0x401 && skeleton->Joints[0].mtxA == 0 && skeleton->Joints[0].mtxB == 0,
          "First joint decoded correctly");
    Check(skeleton->Joints[1].mtxA == 0x0F && skeleton->Joints[1].mtxB == 0x12,
          "Second (mirrored) joint's channels decoded correctly");
}

void TestModelSkeletonFactoryRejectsCountPastEndOfSegment() {
    std::vector<char> body;
    AppendBEU32(body, 5); // claims 5 joints, but no joint data follows

    auto initData = MakeInitData();
    auto file = MakeFile(body, initData);

    ResourceFactoryBinaryModelSkeletonV0 factory;
    Check(factory.ReadResource(file, initData) == nullptr, "A joint count the file cannot hold is rejected");
}

void TestAnimationClipFactoryParsesHeaderAndBitstream() {
    std::vector<char> body;
    AppendBEU32(body, 2); // frame count
    body.push_back(16);   // angle width bits
    body.push_back(1);    // loop flag
    AppendBEU16(body, 0); // padding
    AppendBEU32(body, 3); // frame size bytes
    for (char b : { 0, 1, 2, 3, 4, 5 }) {
        body.push_back(b);
    }
    AppendBEU16(body, 19); // root-motion stride bits
    AppendBEU16(body, 2);  // descriptor count
    AppendBEU32(body, 3);  // root-motion bitstream bytes
    // Descriptors: bit offset, bit count, value offset.
    AppendBEU16(body, 5);
    body.push_back(7);
    AppendBEU16(body, 0x0415);
    AppendBEU16(body, 12);
    body.push_back(0);
    AppendBEU16(body, 31);
    for (char b : { 0x11, 0x22, 0x33 }) {
        body.push_back(b);
    }

    auto initData = MakeInitData();
    auto file = MakeFile(body, initData);

    ResourceFactoryBinaryAnimationClipV1 factory;
    auto resource = factory.ReadResource(file, initData);
    Check(resource != nullptr, "AnimationClip factory returns a resource");

    auto clip = std::static_pointer_cast<AnimationClipResource>(resource);
    Check(clip->FrameCount == 2 && clip->AngleWidthBits == 16 && clip->Loop && clip->FrameSizeBytes == 3,
          "Header fields decoded correctly");
    Check(clip->Bitstream.size() == 6 && clip->Bitstream[0] == 0 && clip->Bitstream[5] == 5,
          "Bitstream copied byte-for-byte");
    Check(clip->RootStrideBits == 19 && clip->RootMotionDescriptors.size() == 2, "Root-motion header decoded");
    Check(clip->RootMotionDescriptors[0].bitOffset == 5 && clip->RootMotionDescriptors[0].bitCount == 7 &&
              clip->RootMotionDescriptors[0].valueOffset == 0x0415 && clip->RootMotionDescriptors[1].bitOffset == 12 &&
              clip->RootMotionDescriptors[1].valueOffset == 31,
          "Root-motion descriptors decoded");
    Check(clip->RootMotionBitstream.size() == 3 && clip->RootMotionBitstream[0] == 0x11 &&
              clip->RootMotionBitstream[2] == 0x33,
          "Root-motion bitstream copied byte-for-byte");
    Check(clip->View.frameCount == 2 && clip->View.loop && clip->View.bitstream == clip->Bitstream.data() &&
              clip->View.frameSizeBytes == 3 && clip->View.rootStrideBits == 19 &&
              clip->View.rootDescriptors == clip->RootMotionDescriptors.data() && clip->View.rootDescriptorCount == 2 &&
              clip->View.rootBitstream == clip->RootMotionBitstream.data(),
          "The model-layer view points at the resource's own data");
}

void TestAnimationClipFactoryRejectsRootMotionPastEndOfSegment() {
    std::vector<char> body;
    AppendBEU32(body, 0); // frame count
    body.push_back(16);
    body.push_back(0);
    AppendBEU16(body, 0);
    AppendBEU32(body, 0); // frame size
    AppendBEU16(body, 19);
    AppendBEU16(body, 4); // claims 4 descriptors, but none follow
    AppendBEU32(body, 0);

    auto initData = MakeInitData();
    auto file = MakeFile(body, initData);

    ResourceFactoryBinaryAnimationClipV1 factory;
    Check(factory.ReadResource(file, initData) == nullptr, "A root-motion block the file cannot hold is rejected");
}

void TestAnimationClipFactoryRejectsBitstreamPastEndOfSegment() {
    std::vector<char> body;
    AppendBEU32(body, 5); // frame count
    body.push_back(16);
    body.push_back(0);
    AppendBEU16(body, 0);
    AppendBEU32(body, 3); // claims 15 bytes of bitstream, but none follows

    auto initData = MakeInitData();
    auto file = MakeFile(body, initData);

    ResourceFactoryBinaryAnimationClipV1 factory;
    Check(factory.ReadResource(file, initData) == nullptr, "A bitstream size the file cannot hold is rejected");
}

// One header node with a child group, empty LOD and BSP tables, a joint entry
// for each, `switchEntries` in the switch table, a sort entry, and an effect
// entry for the group.
std::vector<char> MakeTwoNodeTree(const std::vector<int32_t>& switchEntries) {
    std::vector<char> body;
    AppendBEU32(body, 2); // node count
    AppendBEU32(body, 3); // matrix count

    for (int i = 0; i < 2; i++) {
        AppendBEU32(body, 0x100 + i * 0x20);  // node offset
        body.push_back(i == 0 ? 0x01 : 0x02); // type
        body.push_back(i == 0 ? 0 : 3);       // render type
        AppendBEU16(body, i);                 // matrix
        AppendBEU16(body, 0xFFFF);            // bend matrix
        AppendBEU16(body, 0);
        AppendBEI32(body, i == 0 ? 1 : -1); // child
        AppendBEI32(body, -1);              // next
        AppendBEI32(body, i == 0 ? -1 : 0); // parent
        for (int axis = 0; axis < 3; axis++) {
            AppendBEU32(body, 0);
        }
        AppendBEU32(body, 0xFFFFFFFF); // primary list
        AppendBEU32(body, 0xFFFFFFFF); // secondary list
    }

    AppendBEU32(body, 0); // LOD table
    AppendBEU32(body, 0); // BSP table
    AppendBEU32(body, 2); // joint table
    AppendBEU32(body, 0);
    AppendBEU16(body, 7); // the header's AnimPart
    AppendBEU32(body, 1);
    AppendBEU16(body, 4);

    AppendBEU32(body, static_cast<uint32_t>(switchEntries.size()));
    for (int32_t entry : switchEntries) {
        AppendBEI32(body, entry);
    }

    AppendBEU32(body, 1); // sort table: the header's first group is node 1
    AppendBEU32(body, 0);
    AppendBEI32(body, 1);
    for (float v : { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f }) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        AppendBEU32(body, bits);
    }

    AppendBEU32(body, 1); // effect table: node 1 has a 32x16 image
    AppendBEU32(body, 1);
    for (float v : { 7.0f, 8.0f, 9.0f }) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        AppendBEU32(body, bits);
    }
    for (uint8_t v : { 1, 32, 16, 3, 3, 0, 0, 0 }) {
        body.push_back(static_cast<char>(v));
    }

    AppendBEU32(body, 1); // bounds table: node 0 has a box
    AppendBEU32(body, 0);
    for (float v : { -1.0f, 2.0f, -3.0f, 4.0f, -5.0f, 6.0f }) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        AppendBEU32(body, bits);
    }

    AppendBEU32(body, 1); // hit table: node 1 is a hit sphere naming node 0
    AppendBEU32(body, 1);
    AppendBEI32(body, 5);
    for (float v : { 25.0f, 1.0f, 2.0f, 3.0f }) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        AppendBEU32(body, bits);
    }
    AppendBEI32(body, 0);
    for (float v : { 0.5f, 0.25f }) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        AppendBEU32(body, bits);
    }
    AppendBEU16(body, 0x100);
    AppendBEU16(body, 0);

    AppendBEU32(body, 1); // collision table: node 1 has 3 vertices and 1 collision vertex
    AppendBEU32(body, 1);
    AppendBEU16(body, 3);
    AppendBEU16(body, 1);
    for (int16_t v : { 10, 20, 30, 1 }) {
        AppendBEU16(body, static_cast<uint16_t>(v));
    }
    AppendBEI32(body, 0);
    AppendBEU16(body, 2);
    AppendBEU16(body, 0);
    for (int16_t v : { 2, -1, 1 }) {
        AppendBEU16(body, static_cast<uint16_t>(v));
    }

    AppendBEU32(body, 1); // joint size table: node 0 has a radius
    AppendBEU32(body, 0);
    {
        const float radius = 12.5f;
        uint32_t bits;
        std::memcpy(&bits, &radius, sizeof(bits));
        AppendBEU32(body, bits);
    }
    {
        const float radius = 99.0f; // the model's bounding radius
        uint32_t bits;
        std::memcpy(&bits, &radius, sizeof(bits));
        AppendBEU32(body, bits);
    }
    return body;
}

void TestModelTreeFactoryParsesJointsAndSwitchTable() {
    auto initData = MakeInitData();
    auto file = MakeFile(MakeTwoNodeTree({ 1, -1, 0 }), initData);

    ResourceFactoryBinaryModelTreeV8 factory;
    auto resource = factory.ReadResource(file, initData);
    Check(resource != nullptr, "ModelTree factory returns a resource");

    auto tree = std::static_pointer_cast<ModelTree>(resource);
    Check(tree->Nodes.size() == 2 && tree->MatrixCount == 3, "Node and matrix counts decoded");
    Check(tree->Nodes[0].jointId == 7 && tree->Nodes[1].jointId == 4, "Joint table applied to header and group nodes");
    Check(tree->Switches.size() == 3 && tree->Switches[0] == 1 && tree->Switches[1] == kNoNode &&
              tree->Switches[2] == 0,
          "Switch table decoded to node indices and null entries");
    Check(tree->Nodes[1].renderType == 3 && tree->Nodes[0].renderType == 0, "Render type read from the node record");
    Check(tree->Nodes[0].sortOther == 1 && tree->Nodes[0].sortPoint[2] == 3.0f &&
              tree->Nodes[0].sortPoint2[0] == 4.0f && tree->Nodes[1].sortOther == kNoNode,
          "Sort table applied to its node");
    const ModelNode& effect = tree->Nodes[1];
    Check(effect.effectSize[0] == 7.0f && effect.effectSize[2] == 9.0f && effect.effectTextured &&
              effect.effectWidth == 32 && effect.effectHeight == 16 && effect.effectMipLevels == 3 &&
              effect.effectFormat == 3 && !tree->Nodes[0].effectTextured,
          "Effect table applied to its node");
    const ModelNode& bounds = tree->Nodes[0];
    Check(bounds.hasBounds && bounds.bounds[0] == -1.0f && bounds.bounds[5] == 6.0f && !tree->Nodes[1].hasBounds,
          "Bounds table applied to its node");
    const ModelNode& hit = tree->Nodes[1];
    Check(hit.hasHit && hit.hitPart == 5 && hit.hitRadiusSquared == 25.0f && hit.hitCentre[2] == 3.0f &&
              hit.hitOther == 0 && hit.hitScale1 == 0.5f && hit.hitScale2 == 0.25f && hit.hitFlags == 0x100 &&
              !tree->Nodes[0].hasHit,
          "Hit table applied to its node");
    Check(hit.collision == 0 && tree->Collisions.size() == 1 && tree->Collisions[0].vertexCount == 3 &&
              tree->Collisions[0].collisionVertices.size() == 1 &&
              tree->Collisions[0].collisionVertices[0].position[1] == 20 &&
              tree->Collisions[0].collisionVertices[0].index == 1 &&
              tree->Collisions[0].collisionVertices[0].relatedNode == 0 &&
              tree->Collisions[0].collisionVertices[0].relatedIndex == 2 &&
              tree->Collisions[0].pointUsage == std::vector<int16_t>({ 2, -1, 1 }) && tree->Nodes[0].collision == -1,
          "Collision table applied to its node");
    Check(tree->Nodes[0].jointRadius == 12.5f && tree->Nodes[1].jointRadius == 0.0f,
          "Joint size table applied to its node");
    Check(tree->BoundingRadius == 99.0f, "Bounding radius decoded");
}

void TestModelTreeFactoryRejectsEffectCountPastEndOfSegment() {
    auto body = MakeTwoNodeTree({});
    body.resize(body.size() - 4 - (4 + 8) - (4 + 8 + 16 + 3 * 2) - (4 + 40) - (4 + 28) - 4 -
                24); // the collision, hit, bounds and effect tables
    AppendBEU32(body, 9);                         // claims 9 effect entries, but none follow

    auto initData = MakeInitData();
    auto file = MakeFile(body, initData);

    ResourceFactoryBinaryModelTreeV8 factory;
    Check(factory.ReadResource(file, initData) == nullptr, "An effect count the file cannot hold is rejected");
}

void TestModelTreeFactoryRejectsSwitchPastNodeCount() {
    auto initData = MakeInitData();
    auto file = MakeFile(MakeTwoNodeTree({ 2 }), initData);

    ResourceFactoryBinaryModelTreeV8 factory;
    Check(factory.ReadResource(file, initData) == nullptr, "A switch entry naming a missing node is rejected");
}

void TestModelTreeFactoryRejectsSwitchCountPastEndOfSegment() {
    auto body = MakeTwoNodeTree({});
    body.resize(body.size() - 4 - (4 + 8) - (4 + 8 + 16 + 3 * 2) - (4 + 40) - (4 + 28) - 4 - 24 - 4 - 4 - 4 - 4 -
                24);      // the collision, hit, bounds, effect and sort tables, then the switch count
    AppendBEU32(body, 9); // claims 9 switch entries, but none follow

    auto initData = MakeInitData();
    auto file = MakeFile(body, initData);

    ResourceFactoryBinaryModelTreeV8 factory;
    Check(factory.ReadResource(file, initData) == nullptr, "A switch count the file cannot hold is rejected");
}

} // namespace

void AppendStanTile(std::vector<char>& body, uint32_t idAndRoom, uint16_t link) {
    AppendBEU32(body, idAndRoom);
    AppendBEU16(body, 0x0FFF);
    AppendBEU16(body, 0x3012);
    for (int i = 0; i < 3; i++) {
        AppendBEU16(body, static_cast<uint16_t>(i * 10));
        AppendBEU16(body, static_cast<uint16_t>(-5));
        AppendBEU16(body, static_cast<uint16_t>(i));
        AppendBEU16(body, i == 0 ? link : 0xFFFF);
    }
}

void TestStanFactoryParsesTiles() {
    std::vector<char> body;
    AppendBEU32(body, 2); // tile count
    AppendStanTile(body, 0x08CC3288, 1);
    AppendStanTile(body, 0x08CC3189, 0xFFFF);

    auto initData = MakeInitData();
    auto file = MakeFile(body, initData);

    ResourceFactoryBinaryStanV0 factory;
    auto resource = factory.ReadResource(file, initData);
    Check(resource != nullptr, "Stan factory returns a resource");
    if (resource == nullptr) {
        return;
    }

    auto stan = std::static_pointer_cast<StanResource>(resource);
    Check(stan->Data.tiles.size() == 2, "Tile count decoded correctly");
    const Level::StanTile& tile = stan->Data.tiles[0];
    Check(tile.id == 0x08CC32 && tile.room == 0x88 && tile.PointCount() == 3, "Tile header decoded correctly");
    Check(tile.points[1].x == 10 && tile.points[1].y == -5 && tile.points[2].z == 2, "Points decoded correctly");
    Check(tile.points[0].link == 1 && tile.points[1].link == Level::kNoTile, "Links decoded correctly");
    Check(stan->Data.tiles[1].room == 0x89, "Second tile decoded correctly");
}

void TestStanFactoryRejectsTruncatedTileList() {
    std::vector<char> body;
    AppendBEU32(body, 2); // claims two tiles, holds one
    AppendStanTile(body, 0x08CC3288, 0xFFFF);

    auto initData = MakeInitData();
    auto file = MakeFile(body, initData);

    ResourceFactoryBinaryStanV0 factory;
    Check(factory.ReadResource(file, initData) == nullptr, "A tile count the file cannot hold is rejected");
}

int main() {
    TestStanFactoryParsesTiles();
    TestStanFactoryRejectsTruncatedTileList();
    TestFontFactoryParsesKerningCharsAndPixels();
    TestFontFactoryRejectsGlyphPastEndOfSegment();
    TestTextFactoryResolvesOffsetsToStrings();
    TestModelSkeletonFactoryParsesJoints();
    TestModelSkeletonFactoryRejectsCountPastEndOfSegment();
    TestAnimationClipFactoryParsesHeaderAndBitstream();
    TestAnimationClipFactoryRejectsBitstreamPastEndOfSegment();
    TestAnimationClipFactoryRejectsRootMotionPastEndOfSegment();
    TestModelTreeFactoryParsesJointsAndSwitchTable();
    TestModelTreeFactoryRejectsSwitchPastNodeCount();
    TestModelTreeFactoryRejectsSwitchCountPastEndOfSegment();
    TestModelTreeFactoryRejectsEffectCountPastEndOfSegment();

    if (gFailures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("All resource type tests passed\n");
    return 0;
}
