#include "resource/factory/FontFactory.h"
#include "resource/factory/TextFactory.h"
#include "resource/type/Font.h"
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

} // namespace

int main() {
    TestFontFactoryParsesKerningCharsAndPixels();
    TestFontFactoryRejectsGlyphPastEndOfSegment();
    TestTextFactoryResolvesOffsetsToStrings();

    if (gFailures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("All resource type tests passed\n");
    return 0;
}
