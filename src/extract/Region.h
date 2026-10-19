#pragma once

#include "extract/Error.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace Frigate::Extract {

enum class Region { Us, Eur, Jp };

struct RegionInfo {
    Region region;
    const char* code; // "u", "e", "j": the decomp's suffix for the region
    const char* name; // "USA", "Europe", "Japan": for messages
    const char* sha1; // lowercase hex of the big-endian .z64
    // Added to a US image-table ROM offset to get this region's (images are byte-identical across regions).
    int32_t imageShift;
};

// Every region's dump is this long.
constexpr uint64_t kRomSize = 0xC00000;

const RegionInfo& GetRegionInfo(Region region);
std::span<const RegionInfo> AllRegions();

// Detection is by SHA-1 of the whole file, so a dump is recognised whatever it is named or where it lives.
// A byte-swapped or wrong-sized file gets its own error so the message can say what to do.
std::optional<Region> DetectRegion(std::span<const uint8_t> rom, Error* error = nullptr);
std::optional<Region> DetectRegionOfFile(const std::filesystem::path& path, Error* error = nullptr);

// Cheap enough to run over every file in a directory: anything not exactly kRomSize long is rejected
// before it is read.
bool IsUsRom(const std::filesystem::path& path);
bool IsRegionRom(const std::filesystem::path& path, Region region);

} // namespace Frigate::Extract
