// PackShaders: the f3d.o2r that carries libultraship's Fast3D shader sources (tools/pack_shaders).

#include "PackShaders.h"

#include "extract/Inflate.h"
#include "extract/O2rWriter.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

using namespace Frigate::Extract;
namespace fs = std::filesystem;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// Reads every member of a zip written by O2rWriter, checking CRCs. Enough of the format for these tests.
std::map<std::string, std::vector<uint8_t>> ReadZip(const fs::path& path, bool& ok) {
    std::map<std::string, std::vector<uint8_t>> members;
    ok = false;
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto u16 = [&](size_t at) { return uint32_t(data[at]) | (uint32_t(data[at + 1]) << 8); };
    auto u32 = [&](size_t at) { return u16(at) | (u16(at + 2) << 16); };
    if (data.size() < 22 || u32(data.size() - 22) != 0x06054B50) {
        return members;
    }
    size_t eocd = data.size() - 22;
    size_t count = u16(eocd + 10);
    size_t at = u32(eocd + 16);
    for (size_t i = 0; i < count; i++) {
        if (u32(at) != 0x02014B50) {
            return members;
        }
        uint32_t method = u16(at + 10), crc = u32(at + 16), csize = u32(at + 20), size = u32(at + 24);
        size_t nameLength = u16(at + 28), extraLength = u16(at + 30), commentLength = u16(at + 32);
        size_t local = u32(at + 42);
        std::string name(data.begin() + at + 46, data.begin() + at + 46 + nameLength);
        at += 46 + nameLength + extraLength + commentLength;

        size_t bodyAt = local + 30 + u16(local + 26) + u16(local + 28);
        std::span<const uint8_t> body(data.data() + bodyAt, csize);
        std::vector<uint8_t> bytes;
        if (method == 8) {
            if (!InflateRaw(body, bytes, size).ok() && size != 0) {
                return members;
            }
        } else {
            bytes.assign(body.begin(), body.end());
        }
        if (bytes.size() != size || Crc32(bytes) != crc) {
            return members;
        }
        members[name] = bytes;
    }
    ok = true;
    return members;
}

void TestPacksEachShaderAtItsRelativePath() {
    fs::path dir = fs::temp_directory_path() / "frigate_pack_shaders_test";
    fs::remove_all(dir);
    // "a/b.glsl" sorts before "a.x" (component order), which is where the resource ids come from.
    const std::string binary("x\0y\r\n", 5);
    Write(dir / "shaders" / "opengl" / "default.shader.glsl", "void main() {}");
    Write(dir / "shaders" / "a" / "b.glsl", binary);
    Write(dir / "shaders" / "a.x", "");

    size_t count = 0;
    Error error = PackShaders(dir / "shaders", dir / "f3d.o2r", &count);
    Check(error.ok(), "PackShaders succeeds");
    Check(count == 3, "PackShaders counts the shaders");

    bool ok = false;
    auto members = ReadZip(dir / "f3d.o2r", ok);
    Check(ok, "the archive reads back");
    Check(members.size() == 3, "one member per shader");

    struct Expected {
        const char* name;
        uint64_t id;
        std::string payload;
    };
    const Expected expected[] = {
        { "shaders/a/b.glsl", 0, binary },
        { "shaders/a.x", 1, "" },
        { "shaders/opengl/default.shader.glsl", 2, "void main() {}" },
    };
    for (const Expected& e : expected) {
        auto it = members.find(e.name);
        if (it == members.end()) {
            Check(false, e.name);
            continue;
        }
        ResourceHeader header;
        const std::vector<uint8_t>& bytes = it->second;
        Check(UnpackResourceHeader(bytes, header), "member has a resource header");
        Check(header.type == ResourceTypeCode("SHAD") && header.version == 0 && header.id == e.id &&
                  header.byteOrder == Endianness::Big && !header.isCustom,
              "header fields");
        Check(bytes.size() == kOtrHeaderSize + e.payload.size() &&
                  std::string(bytes.begin() + kOtrHeaderSize, bytes.end()) == e.payload,
              "payload is the source text with no terminator");
    }
    fs::remove_all(dir);
}

void TestRejectsAnEmptyDirectory() {
    fs::path dir = fs::temp_directory_path() / "frigate_pack_shaders_empty";
    fs::remove_all(dir);
    fs::create_directories(dir);
    Check(!PackShaders(dir, dir / "f3d.o2r").ok(), "an empty shader directory is an error");
    Check(!PackShaders(dir / "missing", dir / "f3d.o2r").ok(), "a missing shader directory is an error");
    fs::remove_all(dir);
}

} // namespace

int main() {
    TestPacksEachShaderAtItsRelativePath();
    TestRejectsAnEmptyDirectory();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::puts("PackShaders tests passed");
    return 0;
}
