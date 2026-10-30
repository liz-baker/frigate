// Checks frigate_extract's texture decoder against the game's own decompression code (GameTextureOracle.c, built from
// the GoldenEye decomp) for every non-paletted texture in a US ROM, including the 74 IA4 textures and the lookup-table
// textures the decomp's reference decoder cannot verify (docs/extract-verification.md). Skips without a US ROM in
// FRIGATE_ROMS.

#include "GameTextureOracle.h"

#include "extract/ImageDefs.h"
#include "extract/Region.h"
#include "extract/RomLayout.h"
#include "extract/Texture.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace Frigate::Extract;
namespace fs = std::filesystem;

namespace {

std::optional<fs::path> FindUsRom() {
    const char* env = std::getenv("FRIGATE_ROMS");
    if (env == nullptr) {
        return std::nullopt;
    }
    std::string list = env;
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(separator, start);
        if (end == std::string::npos) {
            end = list.size();
        }
        fs::path location = list.substr(start, end - start);
        start = end + 1;
        std::error_code ec;
        if (fs::is_regular_file(location, ec)) {
            if (IsUsRom(location)) {
                return location;
            }
        } else if (fs::is_directory(location, ec)) {
            for (const auto& entry : fs::directory_iterator(location, ec)) {
                if (entry.is_regular_file() && IsUsRom(entry.path())) {
                    return entry.path();
                }
            }
        }
    }
    return std::nullopt;
}

// The pixel bytes of one row in the decoder's tightly packed output, and the stride the game lays rows out at.
size_t TightRowBytes(TexFormat format, size_t width) {
    switch (format) {
        case TexFormat::RGBA32:
        case TexFormat::RGB24:
            return width * 4;
        case TexFormat::RGBA16:
        case TexFormat::RGB15:
        case TexFormat::IA16:
            return width * 2;
        case TexFormat::IA8:
        case TexFormat::I8:
            return width;
        default:
            return (width + 1) / 2;
    }
}

size_t GameRowBytes(TexFormat format, size_t width) {
    switch (format) {
        case TexFormat::RGBA32:
        case TexFormat::RGB24:
            return ((width + 3) & ~size_t(3)) * 4;
        case TexFormat::RGBA16:
        case TexFormat::RGB15:
        case TexFormat::IA16:
            return ((width + 3) & ~size_t(3)) * 2;
        case TexFormat::IA8:
        case TexFormat::I8:
            return (width + 7) & ~size_t(7);
        default:
            return ((width + 15) & ~size_t(15)) / 2;
    }
}

// The textures the decomp's reference decoder cannot verify (tools/o2r_packer/reference_compare.py, _skip_reason).
const char* ReferenceGap(TexFormat format, int method) {
    if (format == TexFormat::IA4) {
        return "IA4";
    }
    if (method == 6 || method == 7) {
        if (format != TexFormat::RGBA16 && format != TexFormat::RGB15 && format != TexFormat::IA16) {
            return "lookup-from-buffer";
        }
    }
    if (method == 0 || method == 1) {
        return "uncompressed";
    }
    return nullptr;
}

struct Tally {
    int checked = 0;
    int matched = 0;
};

} // namespace

int main() {
    std::optional<fs::path> rom = FindUsRom();
    if (!rom) {
        std::printf("skip: no US ROM (set FRIGATE_ROMS)\n");
        return 77;
    }
    std::ifstream file(*rom, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::span<const ImageDef> defs = ImageDefs();
    std::span<const ImageEntry> table = ImageTable();
    std::vector<uint8_t> game(0x40000);

    int failures = 0;
    int paletted = 0;
    int doubleStrideTextures = 0;
    std::map<std::string, Tally> groups;
    std::map<int, Tally> methods;
    int exampleLimit = 10;

    for (size_t i = 0; i < defs.size(); i++) {
        uint32_t offset = ImageRomOffset(Region::Us, i);
        std::span<const uint8_t> bytes(data.data() + offset, table[i].size);
        if (bytes[0] & 0x40) {
            paletted++;
            continue;
        }

        DecodedTexture decoded;
        Error error = DecodeTexture(bytes, decoded);
        GameTextureInfo info;
        int gameBytes = GameDecodeTexture(bytes.data() + 1, game.data(), game.size(), &info);
        if (!error.ok() || gameBytes == 0) {
            std::fprintf(stderr, "FAIL: %s: extractor error '%s', game bytes %d\n", defs[i].name, error.message.c_str(),
                         gameBytes);
            failures++;
            continue;
        }

        const TextureHeader& header = decoded.header;
        bool same =
            int(header.format) == info.format && int(header.width) == info.width && int(header.height) == info.height;
        std::string where = "header differs";
        bool doubleStride = false;
        if (same) {
            // Rows are compared on their tight bytes; the game's padding after each row is not part of the image. A
            // half-used final byte (odd width, 4-bit formats) has an unused low nibble the two sides may fill
            // differently, so it is masked.
            size_t tight = TightRowBytes(header.format, header.width);
            size_t stride = GameRowBytes(header.format, header.width);
            // The game's channel-plane path steps IA4 rows by a whole row's pixel count in bytes, twice the stride
            // of every other path and format, so the rows of those textures sit one blank row apart.
            bool channelPlanes = info.method >= 2 && info.method != 5 && info.method != 6 && info.method != 7;
            if (header.format == TexFormat::IA4 && channelPlanes) {
                stride *= 2;
                doubleStride = true;
            }
            bool halfByte = (header.format == TexFormat::IA4 || header.format == TexFormat::I4) && (header.width & 1);
            same = decoded.pixels.size() == tight * header.height;
            if (!same) {
                where = "pixel count differs";
            }
            for (size_t y = 0; same && y < header.height; y++) {
                for (size_t x = 0; x < tight; x++) {
                    uint8_t mask = halfByte && x == tight - 1 ? 0xF0 : 0xFF;
                    if ((decoded.pixels[y * tight + x] & mask) != (game[y * stride + x] & mask)) {
                        same = false;
                        char text[96];
                        std::snprintf(text, sizeof(text), "row %zu byte %zu: extractor %02x, game %02x", y, x,
                                      decoded.pixels[y * tight + x], game[y * stride + x]);
                        where = text;
                        break;
                    }
                }
            }
        }

        const char* gap = ReferenceGap(header.format, info.method);
        Tally& group = groups[gap ? gap : "reference-verifiable"];
        group.checked++;
        group.matched += same;
        doubleStrideTextures += same && doubleStride;
        Tally& method = methods[info.method];
        method.checked++;
        method.matched += same;
        if (!same) {
            failures++;
            if (exampleLimit-- > 0) {
                std::fprintf(stderr, "MISMATCH: %s (format %d, %dx%d, method %d): %s\n", defs[i].name, info.format,
                             info.width, info.height, info.method, where.c_str());
            }
        }
    }

    int total = 0;
    int ok = 0;
    for (const auto& [name, tally] : groups) {
        std::printf("%-22s %4d checked, %4d match the game code\n", name.c_str(), tally.checked, tally.matched);
        total += tally.checked;
        ok += tally.matched;
    }
    for (const auto& [method, tally] : methods) {
        std::printf("method %d: %d checked, %d match\n", method, tally.checked, tally.matched);
    }
    std::printf("%d of those match only at the game's doubled IA4 row stride\n", doubleStrideTextures);
    std::printf("%d non-paletted textures checked, %d match; %d paletted textures not covered (zlib path)\n", total, ok,
                paletted);
    return failures == 0 ? 0 : 1;
}
