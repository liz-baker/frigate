#pragma once

#include "extract/Region.h"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace Frigate::Extract {

struct RomSearchResult {
    // The first file that is the US ROM, in directory order and then filename order.
    std::optional<std::filesystem::path> usRom;
    // Files of the right size that are another release.
    struct OtherRegion {
        std::filesystem::path path;
        Region region;
    };
    std::vector<OtherRegion> otherRegions;
    // Files of the right size in a byte-swapped (.v64/.n64) order.
    std::vector<std::filesystem::path> byteSwapped;
    // Files of the right size that are not any known release.
    std::vector<std::filesystem::path> unrecognized;
    // Directories that could not be listed and files that could not be read.
    std::vector<std::filesystem::path> unreadable;
    // The distinct directories that were searched, as given (a directory listed twice, or reached through a
    // symlink, is searched once).
    std::vector<std::filesystem::path> searched;
    // How many files were the size of a ROM and so were hashed.
    size_t hashed = 0;
};

// Looks for the US ROM among the regular files directly inside each directory (no recursion). A file is only
// read when it is exactly kRomSize long, so large unrelated files cost nothing. Stops at the first US ROM.
RomSearchResult FindUsRom(std::span<const std::filesystem::path> directories);

} // namespace Frigate::Extract
