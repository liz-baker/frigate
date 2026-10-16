#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// Load-time rewrite of the GoldenEye display-list texture request (G_SETTEX)
// into the RDP commands that set up the texture. Background and the
// per-type behavior: docs/formats/goldeneye-display-list-resource.md.
//
// Deliberately free of libultraship types so it can be tested with a plain
// compiler; the resource factory that feeds it lives in
// resource/factory/DisplayListFactory.h.
namespace Frigate::TexExpand {

// One display-list entry. A hash reference occupies two entries (command
// word pair, then the 64-bit hash as w0/w1).
struct Word {
    uint32_t w0;
    uint32_t w1;

    bool operator==(const Word&) const = default;
};

// N64 format/size codes (G_IM_FMT_*, G_IM_SIZ_*) and dimensions of the base level.
struct ImageInfo {
    uint8_t format;
    uint8_t size;
    uint16_t width;
    uint16_t height;
    // Paletted images only: hash of the `.pal` resource and its entry count.
    uint64_t paletteHash = 0;
    uint16_t paletteEntries = 0;
    // Mipmapped images only: hash of the `.mip` resource holding the whole
    // chain (layout: docs/formats/goldeneye-texture-mip-chain.md) and its
    // level count, the game's maxlod.
    uint64_t mipHash = 0;
    uint8_t mipLevels = 0;
};

// Number of levels a `.mip` chain of `chainBytes` bytes holds for an image
// with `base`'s format and size, or 0 if no whole number of levels adds up
// to it.
uint8_t MipLevelsForChainSize(const ImageInfo& base, uint32_t chainBytes);

// Resolves the hash of a texture reference; nullopt if it can't be loaded.
using ImageLookup = std::function<std::optional<ImageInfo>(uint64_t hash)>;

struct Stats {
    size_t expanded = 0;
    // Requests left as stored: unhandled type, unresolved image, or an
    // unsupported image format.
    size_t unsupported = 0;
};

// Returns `in` with each supported G_SETTEX and the texture reference that
// follows it replaced by the setup commands. Everything else is copied.
std::vector<Word> ExpandSetTex(const std::vector<Word>& in, const ImageLookup& lookup, Stats* stats = nullptr);

} // namespace Frigate::TexExpand
