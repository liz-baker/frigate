#pragma once

#include "extract/Region.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Frigate::Extract {

// One row of the decomp's scripts/filelist.<region>.csv.
struct FileEntry {
    uint32_t offset; // ROM offset
    uint32_t size;   // bytes in the ROM (compressed size when `compressed`)
    const char* name;
    bool compressed; // 1172-compressed (see Inflate.h)
    bool extract;    // the decomp's own extractor writes it out
};

// One row of the decomp's imagelist.u.csv. Offsets are the US ROM's; use ImageRomOffset() for other regions.
struct ImageEntry {
    uint32_t offset;
    uint32_t size;
};

// Fixed ROM locations the packers need beyond the file list. They are bare facts about one release; a region
// whose values are not worked out yet has `known == false` and zeros.
struct RegionConstants {
    bool known;

    // The compressed data segment (scripts/extract_cdata.sh) and the address the game loads it at.
    uint32_t cdataRomOffset;
    uint32_t cdataRomSize;
    uint32_t cdataLoadAddress;
    // g_GlobalAILists' {address, id} table, and the first list in the segment (the lists run up to the table).
    uint32_t aiListTableAddress;
    uint32_t aiFirstListAddress;

    // The Rareware logo segment, as ROM range and the segment address its display lists refer to.
    uint32_t rarewareLogoRomStart;
    uint32_t rarewareLogoRomEnd;
    uint32_t rarewareLogoSegmentAddress;

    // The global image table (12-byte entries) the HUD and file select pick images from.
    uint32_t hudImageTableRomStart;

    // The gun barrel screen: the RLE backdrop's ROM range, and the game code segment's compressed blob that
    // holds the blood animation.
    uint32_t eyeIntroSightRomStart;
    uint32_t eyeIntroSightRomSize;
    uint32_t eyeIntroGameSegmentRomStart;
};

struct RomLayout {
    Region region;
    std::span<const FileEntry> files;
    RegionConstants constants;

    // Exact match on FileEntry::name; nullptr when absent.
    const FileEntry* FindFile(std::string_view name) const;
    // The entry covering ROM offset `offset`, or nullptr.
    const FileEntry* FindFileAt(uint32_t offset) const;
};

const RomLayout& GetRomLayout(Region region);

// The image table is one list (US offsets); a region's images sit at the same list order, shifted.
std::span<const ImageEntry> ImageTable();
uint32_t ImageRomOffset(Region region, size_t index);

namespace Detail {
// Defined in the generated RomLayoutData.cpp.
extern const FileEntry kUsFiles[];
extern const size_t kUsFileCount;
extern const FileEntry kEurFiles[];
extern const size_t kEurFileCount;
extern const FileEntry kJpFiles[];
extern const size_t kJpFileCount;
extern const ImageEntry kImages[];
extern const size_t kImageCount;
} // namespace Detail

} // namespace Frigate::Extract
