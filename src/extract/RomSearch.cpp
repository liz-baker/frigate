#include "extract/RomSearch.h"

#include <algorithm>
#include <system_error>

namespace Frigate::Extract {

namespace fs = std::filesystem;

RomSearchResult FindUsRom(std::span<const fs::path> directories) {
    RomSearchResult result;
    std::vector<fs::path> seen;
    for (const fs::path& directory : directories) {
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(directory, ec);
        if (ec) {
            canonical = directory;
        }
        if (std::find(seen.begin(), seen.end(), canonical) != seen.end()) {
            continue;
        }
        seen.push_back(canonical);
        result.searched.push_back(directory);

        std::vector<fs::path> files;
        fs::directory_iterator it(directory, ec);
        if (ec) {
            if (fs::exists(directory)) {
                result.unreadable.push_back(directory);
            }
            continue;
        }
        for (const fs::directory_iterator end; it != end; it.increment(ec)) {
            if (ec) {
                result.unreadable.push_back(directory);
                break;
            }
            std::error_code fileEc;
            if (it->is_regular_file(fileEc) && !fileEc) {
                files.push_back(it->path());
            }
        }
        std::sort(files.begin(), files.end());

        for (const fs::path& file : files) {
            std::error_code sizeEc;
            uint64_t size = fs::file_size(file, sizeEc);
            if (sizeEc || size != kRomSize) {
                continue;
            }
            result.hashed++;
            Error error;
            std::optional<Region> region = DetectRegionOfFile(file, &error);
            if (region == Region::Us) {
                result.usRom = file;
                return result;
            }
            if (region) {
                result.otherRegions.push_back({ file, *region });
            } else if (error.code == ErrorCode::RomByteOrder) {
                result.byteSwapped.push_back(file);
            } else if (error.code == ErrorCode::FileOpen) {
                result.unreadable.push_back(file);
            } else if (error.code == ErrorCode::RomUnrecognized) {
                result.unrecognized.push_back(file);
            }
        }
    }
    return result;
}

} // namespace Frigate::Extract
