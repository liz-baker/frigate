#include "extract/O2rWriter.h"

#include "extract/Inflate.h"

#include <cstring>

namespace Frigate::Extract {

namespace {

constexpr uint16_t kMethodStored = 0;
constexpr uint16_t kMethodDeflate = 8;
// 1980-01-01 00:00: members carry no timestamp so the same input gives the same archive.
constexpr uint16_t kDosTime = 0;
constexpr uint16_t kDosDate = (0 << 9) | (1 << 5) | 1;

void Put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(uint8_t(value));
    out.push_back(uint8_t(value >> 8));
}

void Put32(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 0; i < 4; i++) {
        out.push_back(uint8_t(value >> (8 * i)));
    }
}

void PutOrdered(std::vector<uint8_t>& out, uint64_t value, int bytes, Endianness order) {
    for (int i = 0; i < bytes; i++) {
        int shift = (order == Endianness::Big) ? 8 * (bytes - 1 - i) : 8 * i;
        out.push_back(uint8_t(value >> shift));
    }
}

uint64_t GetOrdered(const uint8_t* data, int bytes, Endianness order) {
    uint64_t value = 0;
    for (int i = 0; i < bytes; i++) {
        int shift = (order == Endianness::Big) ? 8 * (bytes - 1 - i) : 8 * i;
        value |= uint64_t(data[i]) << shift;
    }
    return value;
}

} // namespace

uint32_t ResourceTypeCode(std::string_view fourcc) {
    if (fourcc.size() != 4) {
        return 0;
    }
    return (uint32_t(uint8_t(fourcc[0])) << 24) | (uint32_t(uint8_t(fourcc[1])) << 16) |
           (uint32_t(uint8_t(fourcc[2])) << 8) | uint32_t(uint8_t(fourcc[3]));
}

std::vector<uint8_t> PackResourceHeader(const ResourceHeader& header) {
    std::vector<uint8_t> out;
    out.reserve(kOtrHeaderSize);
    out.push_back(uint8_t(header.byteOrder));
    out.push_back(header.isCustom ? 1 : 0);
    out.push_back(0);
    out.push_back(0);
    PutOrdered(out, header.type, 4, header.byteOrder);
    PutOrdered(out, header.version, 4, header.byteOrder);
    PutOrdered(out, header.id, 8, header.byteOrder);
    out.resize(kOtrHeaderSize, 0);
    return out;
}

bool UnpackResourceHeader(std::span<const uint8_t> data, ResourceHeader& header) {
    if (data.size() < kOtrHeaderFieldsSize || data[0] > 1) {
        return false;
    }
    header.byteOrder = Endianness(data[0]);
    header.isCustom = data[1] != 0;
    header.type = uint32_t(GetOrdered(&data[4], 4, header.byteOrder));
    header.version = uint32_t(GetOrdered(&data[8], 4, header.byteOrder));
    header.id = GetOrdered(&data[12], 8, header.byteOrder);
    return true;
}

Error O2rWriter::Write(const void* data, size_t size) {
    mFile.write(static_cast<const char*>(data), std::streamsize(size));
    if (!mFile) {
        return Error::Make(ErrorCode::FileWrite, "Could not write " + mPath.string() + ". Is the disk full?");
    }
    mOffset += size;
    return {};
}

Error O2rWriter::Open(const std::filesystem::path& path) {
    mPath = path;
    mMembers.clear();
    mOffset = 0;
    mFile.open(path, std::ios::binary | std::ios::trunc);
    if (!mFile) {
        return Error::Make(ErrorCode::FileWrite, "Could not create " + path.string());
    }
    mOpen = true;
    return {};
}

Error O2rWriter::AddRawFile(std::string_view virtualPath, std::span<const uint8_t> data) {
    if (!mOpen) {
        return Error::Make(ErrorCode::Archive, "Archive is not open.");
    }
    if (virtualPath.empty() || virtualPath.size() > 0xFFFF || data.size() > 0xFFFFFFFFull / 2 ||
        mOffset > 0xFFFFFFFFull / 2 || mMembers.size() >= 0xFFFF) {
        return Error::Make(ErrorCode::Archive, "Archive member is out of range: " + std::string(virtualPath));
    }

    // Deflate when it helps, otherwise store; readers handle both.
    std::vector<uint8_t> deflated;
    if (Error err = DeflateRaw(data, deflated); !err.ok()) {
        return err;
    }
    uint16_t method = kMethodDeflate;
    std::span<const uint8_t> body = deflated;
    if (deflated.size() >= data.size()) {
        method = kMethodStored;
        body = data;
    }

    Member member;
    member.name = std::string(virtualPath);
    member.crc = Crc32(data);
    member.compressedSize = uint32_t(body.size());
    member.size = uint32_t(data.size());
    member.headerOffset = uint32_t(mOffset);
    member.method = method;

    std::vector<uint8_t> local;
    Put32(local, 0x04034B50);
    Put16(local, 20); // version needed
    Put16(local, 0);  // flags
    Put16(local, method);
    Put16(local, kDosTime);
    Put16(local, kDosDate);
    Put32(local, member.crc);
    Put32(local, member.compressedSize);
    Put32(local, member.size);
    Put16(local, uint16_t(member.name.size()));
    Put16(local, 0); // extra length
    local.insert(local.end(), member.name.begin(), member.name.end());

    if (Error err = Write(local.data(), local.size()); !err.ok()) {
        return err;
    }
    if (Error err = Write(body.data(), body.size()); !err.ok()) {
        return err;
    }
    mMembers.push_back(std::move(member));
    return {};
}

Error O2rWriter::AddResource(std::string_view virtualPath, uint32_t type, uint32_t version, uint64_t id,
                             std::span<const uint8_t> payload, Endianness byteOrder, bool isCustom) {
    ResourceHeader header;
    header.byteOrder = byteOrder;
    header.isCustom = isCustom;
    header.type = type;
    header.version = version;
    header.id = id;

    std::vector<uint8_t> bytes = PackResourceHeader(header);
    if (type == kResourceTypeBlob) {
        PutOrdered(bytes, payload.size(), 4, byteOrder);
    }
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return AddRawFile(virtualPath, bytes);
}

Error O2rWriter::AddResource(std::string_view virtualPath, std::string_view fourcc, uint32_t version, uint64_t id,
                             std::span<const uint8_t> payload, Endianness byteOrder, bool isCustom) {
    uint32_t type = ResourceTypeCode(fourcc);
    if (type == 0) {
        return Error::Make(ErrorCode::Archive, "A resource type tag must be exactly 4 characters.");
    }
    return AddResource(virtualPath, type, version, id, payload, byteOrder, isCustom);
}

Error O2rWriter::Close() {
    if (!mOpen) {
        return Error::Make(ErrorCode::Archive, "Archive is not open.");
    }
    mOpen = false;

    uint32_t directoryStart = uint32_t(mOffset);
    std::vector<uint8_t> directory;
    for (const Member& member : mMembers) {
        Put32(directory, 0x02014B50);
        Put16(directory, 20); // version made by
        Put16(directory, 20); // version needed
        Put16(directory, 0);  // flags
        Put16(directory, member.method);
        Put16(directory, kDosTime);
        Put16(directory, kDosDate);
        Put32(directory, member.crc);
        Put32(directory, member.compressedSize);
        Put32(directory, member.size);
        Put16(directory, uint16_t(member.name.size()));
        Put16(directory, 0); // extra length
        Put16(directory, 0); // comment length
        Put16(directory, 0); // disk number
        Put16(directory, 0); // internal attributes
        Put32(directory, 0); // external attributes
        Put32(directory, member.headerOffset);
        directory.insert(directory.end(), member.name.begin(), member.name.end());
    }
    uint32_t directorySize = uint32_t(directory.size());
    Put32(directory, 0x06054B50);
    Put16(directory, 0); // this disk
    Put16(directory, 0); // directory disk
    Put16(directory, uint16_t(mMembers.size()));
    Put16(directory, uint16_t(mMembers.size()));
    Put32(directory, directorySize);
    Put32(directory, directoryStart);
    Put16(directory, 0); // comment length

    if (Error err = Write(directory.data(), directory.size()); !err.ok()) {
        return err;
    }
    mFile.close();
    if (mFile.fail()) {
        return Error::Make(ErrorCode::FileWrite, "Could not finish writing " + mPath.string());
    }
    return {};
}

} // namespace Frigate::Extract
