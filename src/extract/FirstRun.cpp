#include "extract/FirstRun.h"

#include "extract/RomSearch.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <system_error>

namespace Frigate::Extract {

namespace fs = std::filesystem;

namespace {

// The four archives are about 15 MiB; the rest is slack for the one being written.
constexpr uint64_t kRequiredSpace = 40ull << 20;

uint32_t ReadLe32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

void Log(const FirstRunOptions& options, const std::string& text) {
    if (options.log) {
        options.log(text);
    }
}

std::string JoinPaths(const std::vector<fs::path>& paths) {
    std::string text;
    for (const fs::path& path : paths) {
        text += (text.empty() ? "" : ", ") + path.string();
    }
    return text;
}

std::string NotFoundMessage(const FirstRunOptions& options, const std::vector<std::string>& missing,
                            const RomSearchResult& search) {
    std::string text = "The game data is not extracted yet (" + options.archiveDir.string() + " is missing";
    for (size_t i = 0; i < missing.size(); i++) {
        text += (i == 0 ? " " : ", ") + missing[i];
    }
    text += ").\n";
    text += "No US GoldenEye 007 ROM (an unmodified 12 MiB .z64) was found in " +
            std::string(search.searched.size() == 1 ? "this folder: " : "these folders: ") +
            JoinPaths(search.searched) + "\n";
    for (const auto& other : search.otherRegions) {
        text += "Found the " + std::string(GetRegionInfo(other.region).name) + " ROM " + other.path.string() +
                ", but only the USA release is supported yet.\n";
    }
    for (const fs::path& path : search.byteSwapped) {
        text += path.string() + " is a byte-swapped (.v64 or .n64) dump; convert it to the big-endian .z64 format.\n";
    }
    if (!search.unrecognized.empty()) {
        text += "Not a supported ROM despite the right size: " + JoinPaths(search.unrecognized) + ".\n";
    }
    if (!search.unreadable.empty()) {
        text += "Could not read " + JoinPaths(search.unreadable) + ".\n";
    }
    text += "Put the US ROM in " + std::string(search.searched.size() == 1 ? "that folder" : "one of those folders") +
            ", or run with --rom <path to the ROM>.";
    return text;
}

// The message for a ROM named with --rom that cannot be used.
std::string ExplicitRomMessage(const fs::path& rom, const Error& error, std::optional<Region> region) {
    if (region) {
        return rom.string() + " is the " + GetRegionInfo(*region).name +
               " ROM, but only the USA release is supported yet.";
    }
    switch (error.code) {
        case ErrorCode::FileOpen:
            return "Could not read the ROM " + rom.string() + ": " + error.message;
        default:
            return rom.string() + ": " + error.message;
    }
}

} // namespace

bool IsArchiveComplete(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        return false;
    }
    uint64_t size = fs::file_size(path, ec);
    if (ec || size < 22) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    // The record is 22 bytes plus a comment of up to 65535.
    uint64_t tail = std::min<uint64_t>(size, 22 + 65535);
    std::vector<uint8_t> bytes(tail);
    file.seekg(std::streamoff(size - tail));
    file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(tail));
    if (uint64_t(file.gcount()) != tail) {
        return false;
    }
    for (size_t i = bytes.size() - 22 + 1; i-- > 0;) {
        if (bytes[i] == 'P' && bytes[i + 1] == 'K' && bytes[i + 2] == 5 && bytes[i + 3] == 6) {
            uint64_t recordOffset = size - tail + i;
            uint64_t directorySize = ReadLe32(&bytes[i + 12]);
            uint64_t directoryOffset = ReadLe32(&bytes[i + 16]);
            return directorySize == 0xFFFFFFFFu || directoryOffset == 0xFFFFFFFFu ||
                   directoryOffset + directorySize <= recordOffset;
        }
    }
    return false;
}

std::vector<std::string> MissingArchives(const fs::path& directory) {
    std::vector<std::string> missing;
    for (size_t i = 0; i < kArchiveCount; i++) {
        const char* name = ArchiveFileName(ArchiveId(i));
        if (!IsArchiveComplete(directory / name)) {
            missing.push_back(name);
        }
    }
    return missing;
}

FirstRunResult EnsureArchives(const FirstRunOptions& options) {
    FirstRunResult result;
    std::vector<std::string> missing = MissingArchives(options.archiveDir);
    if (missing.empty()) {
        return result;
    }
    result.status = FirstRunStatus::Failed;
    Log(options, "archives in " + options.archiveDir.string() + " are incomplete (" + std::to_string(missing.size()) +
                     " of " + std::to_string(kArchiveCount) + " missing or damaged); looking for the ROM");

    fs::path rom;
    if (options.rom) {
        Error error;
        std::optional<Region> region = DetectRegionOfFile(*options.rom, &error);
        if (region != Region::Us) {
            result.message = ExplicitRomMessage(*options.rom, error, region);
            return result;
        }
        rom = *options.rom;
    } else {
        RomSearchResult search = FindUsRom(options.searchDirs);
        Log(options, "searched " + JoinPaths(search.searched) + ", checked " + std::to_string(search.hashed) +
                         " file(s) of ROM size");
        if (!search.usRom) {
            result.message = NotFoundMessage(options, missing, search);
            return result;
        }
        rom = *search.usRom;
    }
    result.rom = rom;
    Log(options, "extracting the US ROM " + rom.string() + " into " + options.archiveDir.string());

    std::error_code ec;
    fs::create_directories(options.archiveDir, ec);
    if (ec) {
        result.message = "Could not create " + options.archiveDir.string() + ": " + ec.message();
        return result;
    }
    fs::space_info space = fs::space(options.archiveDir, ec);
    if (!ec && space.available < kRequiredSpace) {
        result.message = "Not enough free space in " + options.archiveDir.string() + ": extracting needs about " +
                         "15 MiB and " + std::to_string(space.available >> 20) + " MiB is free.";
        return result;
    }

    Extractor extractor;
    RegisterDefaultStages(extractor);
    Options extract;
    extract.romPath = rom;
    extract.outputDir = options.archiveDir;
    extract.region = Region::Us;
    extract.progress = options.progress;
    extract.cancel = options.cancel;
    Result run = extractor.Run(extract);
    if (run.error.code == ErrorCode::Cancelled) {
        result.status = FirstRunStatus::Cancelled;
        result.message = "Extraction was cancelled; no archives were written.";
        return result;
    }
    if (!run.error.ok()) {
        result.message = "Extracting " + rom.string() + " failed: " + run.error.message;
        if (run.error.code == ErrorCode::FileWrite) {
            result.message += "\nCheck that " + options.archiveDir.string() + " is writable and has free space.";
        }
        return result;
    }
    // A run that finished but left the set incomplete would loop forever on the next start.
    if (!MissingArchives(options.archiveDir).empty()) {
        result.message = "Extraction finished but the archives in " + options.archiveDir.string() + " are incomplete.";
        return result;
    }
    result.status = FirstRunStatus::Extracted;
    Log(options, "extracted " + std::to_string(run.archives.size()) + " archives");
    return result;
}

} // namespace Frigate::Extract
