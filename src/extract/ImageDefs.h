#pragma once

#include <cstdint>
#include <span>

namespace Frigate::Extract {

// The decomp's HIT_TYPE enum: the surface a shot hits. tools/gen_rom_layout.py checks this list against the decomp.
enum HitType : uint8_t {
    HIT_DEFAULT,
    HIT_STONE,
    HIT_WOOD,
    HIT_METAL,
    HIT_GLASS,
    HIT_WATER,
    HIT_SNOW,
    HIT_DIRT,
    HIT_MUD,
    HIT_TILE,
    HIT_METALOBJ,
    HIT_CHR,
    HIT_GLASS_XLU,
};

// One IMAGE() line of the decomp's images.def. The index is the game's texture number and the position in
// ImageTable() (the image's place in the ROM).
struct ImageDef {
    const char* name;
    HitType hitSound;  // the surface type the hit sound is chosen by
    HitType hitImpact; // the surface type the impact effect is chosen by
    // Where a detail tile reads this image, four 4-bit fields (docs/formats/goldeneye-display-list-resource.md).
    uint8_t detail[4];

    // The fields packed first one highest, as the display list factory takes them.
    uint16_t PackedDetail() const {
        return uint16_t(((detail[0] & 0xF) << 12) | ((detail[1] & 0xF) << 8) | ((detail[2] & 0xF) << 4) |
                        (detail[3] & 0xF));
    }
};

std::span<const ImageDef> ImageDefs();

namespace Detail {
// Defined in the generated ImageDefData.cpp.
extern const ImageDef kImageDefs[];
extern const size_t kImageDefCount;
} // namespace Detail

} // namespace Frigate::Extract
