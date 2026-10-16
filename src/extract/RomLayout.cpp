#include "extract/RomLayout.h"

namespace Frigate::Extract {

namespace {

// Values copied out of the Python packers' module constants (pack_ai_lists.py, pack_rarewarelogo.py,
// pack_hud_images.py, pack_eyeintro.py), which still carry their own copies.
constexpr RegionConstants kUsConstants = {
    true,
    137616,     // cdataRomOffset
    71760,      // cdataRomSize
    0x80020D90, // cdataLoadAddress
    0x8003744C, // aiListTableAddress
    0x80037070, // aiFirstListAddress
    0x29E560,   // rarewareLogoRomStart
    0x2A4D50,   // rarewareLogoRomEnd
    0x02000000, // rarewareLogoSegmentAddress
    0x29D160,   // hudImageTableRomStart
    0x2A4D50,   // eyeIntroSightRomStart
    107904,     // eyeIntroSightRomSize
    0x21990,    // eyeIntroGameSegmentRomStart
};

constexpr RegionConstants kUnknownConstants = {};

const RomLayout kLayouts[3] = {
    { Region::Us, { Detail::kUsFiles, Detail::kUsFileCount }, kUsConstants },
    { Region::Eur, { Detail::kEurFiles, Detail::kEurFileCount }, kUnknownConstants },
    { Region::Jp, { Detail::kJpFiles, Detail::kJpFileCount }, kUnknownConstants },
};

} // namespace

const FileEntry* RomLayout::FindFile(std::string_view wanted) const {
    for (const FileEntry& entry : files) {
        if (wanted == entry.name) {
            return &entry;
        }
    }
    return nullptr;
}

const FileEntry* RomLayout::FindFileAt(uint32_t offset) const {
    for (const FileEntry& entry : files) {
        if (offset >= entry.offset && offset - entry.offset < entry.size) {
            return &entry;
        }
    }
    return nullptr;
}

const RomLayout& GetRomLayout(Region region) {
    return kLayouts[size_t(region)];
}

std::span<const ImageEntry> ImageTable() {
    return { Detail::kImages, Detail::kImageCount };
}

uint32_t ImageRomOffset(Region region, size_t index) {
    return uint32_t(int64_t(Detail::kImages[index].offset) + GetRegionInfo(region).imageShift);
}

} // namespace Frigate::Extract
