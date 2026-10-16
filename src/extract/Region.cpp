#include "extract/Region.h"

#include "extract/Sha1.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>

namespace Frigate::Extract {

namespace {

constexpr std::array<RegionInfo, 3> kRegions = { {
    { Region::Us, "u", "USA", "abe01e4aeb033b6c0836819f549c791b26cfde83", 0 },
    { Region::Eur, "e", "Europe", "167c3c433dec1f1eb921736f7d53fac8cb45ee31", -1456 },
    { Region::Jp, "j", "Japan", "2a5dade32f7fad6c73c659d2026994632c1b3174", 4704 },
} };

// The first word of the other two N64 dump byte orders (.z64 starts 80 37 12 40).
constexpr uint8_t kV64Magic[4] = { 0x37, 0x80, 0x40, 0x12 };
constexpr uint8_t kN64Magic[4] = { 0x40, 0x12, 0x37, 0x80 };

bool SameBytes(const uint8_t* a, const uint8_t* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

void SetError(Error* error, ErrorCode code, std::string message) {
    if (error != nullptr) {
        *error = Error::Make(code, std::move(message));
    }
}

// False, with *error set, when the header alone rules the file out.
bool CheckHeader(const uint8_t* header, Error* error) {
    if (SameBytes(header, kV64Magic) || SameBytes(header, kN64Magic)) {
        SetError(error, ErrorCode::RomByteOrder,
                 "This ROM is in a byte-swapped format (.v64 or .n64). Convert it to the big-endian .z64 format.");
        return false;
    }
    return true;
}

std::optional<Region> MatchHash(const Sha1::Digest& digest, Error* error) {
    std::string hex = Sha1::ToHex(digest);
    for (const RegionInfo& info : kRegions) {
        if (hex == info.sha1) {
            return info.region;
        }
    }
    SetError(error, ErrorCode::RomUnrecognized,
             "This file is not a supported GoldenEye 007 ROM. Use an unmodified USA, Europe or Japan .z64 dump.");
    return std::nullopt;
}

} // namespace

const RegionInfo& GetRegionInfo(Region region) {
    return kRegions[size_t(region)];
}

std::span<const RegionInfo> AllRegions() {
    return kRegions;
}

std::optional<Region> DetectRegion(std::span<const uint8_t> rom, Error* error) {
    if (rom.size() != kRomSize) {
        SetError(error, ErrorCode::RomSize, "This file is not the size of a GoldenEye 007 ROM (12 MiB).");
        return std::nullopt;
    }
    if (!CheckHeader(rom.data(), error)) {
        return std::nullopt;
    }
    return MatchHash(Sha1::Of(rom.data(), rom.size()), error);
}

std::optional<Region> DetectRegionOfFile(const std::filesystem::path& path, Error* error) {
    std::error_code ec;
    uint64_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        SetError(error, ErrorCode::FileOpen, "Could not read " + path.string() + ": " + ec.message());
        return std::nullopt;
    }
    if (size != kRomSize) {
        SetError(error, ErrorCode::RomSize, "This file is not the size of a GoldenEye 007 ROM (12 MiB).");
        return std::nullopt;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        SetError(error, ErrorCode::FileOpen, "Could not open " + path.string());
        return std::nullopt;
    }
    Sha1 hash;
    std::array<uint8_t, 1 << 16> chunk;
    bool first = true;
    uint64_t remaining = size;
    while (remaining != 0) {
        size_t want = size_t(std::min<uint64_t>(remaining, chunk.size()));
        file.read(reinterpret_cast<char*>(chunk.data()), std::streamsize(want));
        if (size_t(file.gcount()) != want) {
            SetError(error, ErrorCode::FileOpen, "Could not read all of " + path.string());
            return std::nullopt;
        }
        if (first && !CheckHeader(chunk.data(), error)) {
            return std::nullopt;
        }
        first = false;
        hash.Update(chunk.data(), want);
        remaining -= want;
    }
    return MatchHash(hash.Finish(), error);
}

bool IsRegionRom(const std::filesystem::path& path, Region region) {
    std::optional<Region> found = DetectRegionOfFile(path);
    return found && *found == region;
}

bool IsUsRom(const std::filesystem::path& path) {
    return IsRegionRom(path, Region::Us);
}

} // namespace Frigate::Extract
