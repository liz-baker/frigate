#pragma once

#include "extract/Error.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frigate::Extract {

// libultraship's OTR_HEADER_SIZE and the part of it that carries fields. Layout: docs/formats/o2r-resource-header.md.
constexpr size_t kOtrHeaderSize = 64;
constexpr size_t kOtrHeaderFieldsSize = 20;

enum class Endianness : uint8_t { Little = 0, Big = 1 };

// A 4-character tag packed as a big-endian uint32 ("OBLB" == 0x4F424C42). Returns 0 for a tag of any other length.
uint32_t ResourceTypeCode(std::string_view fourcc);

constexpr uint32_t kResourceTypeBlob = 0x4F424C42;

struct ResourceHeader {
    Endianness byteOrder = Endianness::Big;
    bool isCustom = false;
    uint32_t type = 0;
    uint32_t version = 0;
    uint64_t id = 0;
};

// The 64-byte header (fields, then zero padding).
std::vector<uint8_t> PackResourceHeader(const ResourceHeader& header);
// Reads the fields back; false when `data` is shorter than kOtrHeaderFieldsSize or the byte order byte is invalid.
bool UnpackResourceHeader(std::span<const uint8_t> data, ResourceHeader& header);

// Writes a .o2r (a zip of resources). Mirrors tools/o2r_packer/o2r_writer.py: each resource is stored under its
// virtual path as header + payload, and a Blob's payload is preceded by its uint32 length.
//
// The archive is written in place; call Close() to finish it (the destructor does not, so an abandoned writer
// leaves an unreadable file rather than a truncated one that looks complete).
class O2rWriter {
  public:
    O2rWriter() = default;
    O2rWriter(const O2rWriter&) = delete;
    O2rWriter& operator=(const O2rWriter&) = delete;

    Error Open(const std::filesystem::path& path);

    Error AddResource(std::string_view virtualPath, uint32_t type, uint32_t version, uint64_t id,
                      std::span<const uint8_t> payload, Endianness byteOrder = Endianness::Big, bool isCustom = false);
    Error AddResource(std::string_view virtualPath, std::string_view fourcc, uint32_t version, uint64_t id,
                      std::span<const uint8_t> payload, Endianness byteOrder = Endianness::Big, bool isCustom = false);

    // A member with no resource header.
    Error AddRawFile(std::string_view virtualPath, std::span<const uint8_t> data);

    // Writes the zip's central directory. Required.
    Error Close();

    bool IsOpen() const {
        return mOpen;
    }
    size_t MemberCount() const {
        return mMembers.size();
    }

  private:
    struct Member {
        std::string name;
        uint32_t crc;
        uint32_t compressedSize;
        uint32_t size;
        uint32_t headerOffset;
        uint16_t method;
    };

    Error Write(const void* data, size_t size);

    std::ofstream mFile;
    std::filesystem::path mPath;
    std::vector<Member> mMembers;
    uint64_t mOffset = 0;
    bool mOpen = false;
};

} // namespace Frigate::Extract
