#include "extract/AnimationClip.h"
#include "extract/DecompTables.h"
#include "extract/PackCommon.h"

#include <algorithm>
#include <cstring>

// Payload layout: docs/formats/goldeneye-animation-clip-resource.md.
namespace Frigate::Extract {

namespace {

constexpr const char* kClipResourceType = "GEAN";
constexpr uint32_t kClipResourceVersion = 1;

// `ModelAnimBitField` in the ROM's table: bitOffset (u16), bitCount (u8), padding, valueOffset (u16 at +4).
constexpr size_t kDescriptorSourceSize = 6;

// The words of an ANIM_DATA array: the entries offset (a link-time pointer in the decomp), then the header fields.
constexpr size_t kHeaderWords = 5;
constexpr size_t kFlagsWord = 1;
constexpr size_t kDescriptorsWord = 2;
constexpr size_t kSizesWord = 3;
constexpr size_t kBitstreamWord = 4;

uint32_t Be32(std::span<const uint8_t> bytes, size_t at) {
    return uint32_t(bytes[at]) << 24 | uint32_t(bytes[at + 1]) << 16 | uint32_t(bytes[at + 2]) << 8 | bytes[at + 3];
}

uint32_t Be16(std::span<const uint8_t> bytes, size_t at) {
    return uint32_t(bytes[at]) << 8 | bytes[at + 1];
}

void Put16(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value));
}

void Put32(std::vector<uint8_t>& out, uint32_t value) {
    Put16(out, value >> 16);
    Put16(out, value);
}

// A clip's root motion: its descriptors and bitstream, read from the words of the data file. A clip whose offsets
// fall outside the table (e.g. one with no frames) has no descriptors and an empty bitstream.
struct RootMotion {
    size_t descriptorsAt = 0;
    size_t descriptorCount = 0;
    size_t bitstreamAt = 0;
    size_t bitstreamSize = 0;
};

RootMotion FindRootMotion(size_t tableSize, uint64_t frameCount, uint64_t strideBits, uint64_t descriptorsOffset,
                          uint64_t bitstreamOffset) {
    uint64_t byteCount = (frameCount * strideBits + 7) / 8;
    uint64_t descriptorCount =
        bitstreamOffset > descriptorsOffset ? (bitstreamOffset - descriptorsOffset) / kDescriptorSourceSize : 0;
    uint64_t descriptorsEnd = descriptorsOffset + descriptorCount * kDescriptorSourceSize;
    if (descriptorCount == 0 || bitstreamOffset + byteCount > tableSize || descriptorsEnd > tableSize) {
        return {};
    }
    return { size_t(descriptorsOffset), size_t(descriptorCount), size_t(bitstreamOffset), size_t(byteCount) };
}

} // namespace

Error EncodeAnimationClip(std::span<const uint8_t> table, std::span<const uint8_t> entries, uint32_t headerOffset,
                          uint32_t entryOffset, std::vector<uint8_t>& out) {
    size_t headerEnd = size_t(headerOffset) + kHeaderWords * 4;
    if (headerEnd > table.size()) {
        return Error::Make(ErrorCode::Corrupt, "An animation clip's header lies outside the animation data.");
    }
    auto word = [&](size_t index) { return Be32(table, headerOffset + index * 4); };

    uint32_t flags = word(kFlagsWord);
    uint32_t frameCount = flags >> 16;
    uint32_t angleBits = (flags >> 8) & 0xFF;
    uint32_t loop = flags & 1;

    uint32_t sizes = word(kSizesWord);
    uint32_t frameSize = (sizes & 0xFFFF) / 8;
    uint32_t strideBits = sizes >> 16;
    RootMotion root =
        FindRootMotion(table.size(), frameCount, strideBits, word(kDescriptorsWord), word(kBitstreamWord));

    // The entries blob is cut short rather than overrun, like a slice.
    uint64_t at = std::min<uint64_t>(entryOffset, entries.size());
    uint64_t size = std::min<uint64_t>(uint64_t(frameCount) * frameSize, entries.size() - at);

    out.clear();
    Put32(out, frameCount);
    out.push_back(uint8_t(angleBits));
    out.push_back(uint8_t(loop));
    Put16(out, 0);
    Put32(out, frameSize);
    out.insert(out.end(), entries.begin() + at, entries.begin() + at + size);

    Put16(out, strideBits);
    Put16(out, uint32_t(root.descriptorCount));
    Put32(out, uint32_t(root.bitstreamSize));
    for (size_t i = 0; i < root.descriptorCount; i++) {
        size_t d = root.descriptorsAt + i * kDescriptorSourceSize;
        Put16(out, Be16(table, d));
        out.push_back(table[d + 2]);
        Put16(out, Be16(table, d + 4));
    }
    out.insert(out.end(), table.begin() + root.bitstreamAt, table.begin() + root.bitstreamAt + root.bitstreamSize);
    return {};
}

Error PackAnimationClips(StageContext& context, O2rWriter& writer) {
    if (Error err = RequireKnownRegion(context, "animations"); !err.ok()) {
        return err;
    }
    const FileEntry* dataFile = context.layout().FindFile("assets/animationtable_data.bin");
    const FileEntry* entriesFile = context.layout().FindFile("assets/animationtable_entries.bin");
    if (dataFile == nullptr || entriesFile == nullptr || dataFile->compressed || entriesFile->compressed) {
        return Error::Make(ErrorCode::Corrupt, "The animation files are missing from the ROM's file list.");
    }
    std::span<const uint8_t> data;
    std::span<const uint8_t> entries;
    if (Error err = context.RomRange(dataFile->offset, dataFile->size, data); !err.ok()) {
        return err;
    }
    if (Error err = context.RomRange(entriesFile->offset, entriesFile->size, entries); !err.ok()) {
        return err;
    }

    // The root motion offsets address the table with each header's first word (the entries offset) read as 0.
    std::vector<uint8_t> table(data.begin(), data.end());
    for (size_t i = 0; i < Detail::kAnimationClipCount; i++) {
        uint32_t at = Detail::kAnimationClips[i].headerOffset;
        if (size_t(at) + kHeaderWords * 4 > table.size()) {
            return Error::Make(ErrorCode::Corrupt, "An animation clip's header lies outside the animation data.");
        }
        std::memset(&table[at], 0, 4);
    }

    std::vector<uint8_t> payload;
    for (size_t i = 0; i < Detail::kAnimationClipCount; i++) {
        if (context.Cancelled()) {
            return Error::Make(ErrorCode::Cancelled, "Cancelled.");
        }
        const AnimationClipRow& clip = Detail::kAnimationClips[i];
        context.Report(i, Detail::kAnimationClipCount, clip.name);
        if (Error err = EncodeAnimationClip(table, entries, clip.headerOffset, Be32(data, clip.headerOffset), payload);
            !err.ok()) {
            return err;
        }
        if (Error err = writer.AddResource(std::string("animations/") + clip.name, kClipResourceType,
                                           kClipResourceVersion, 0, payload);
            !err.ok()) {
            return err;
        }
    }
    context.Report(Detail::kAnimationClipCount, Detail::kAnimationClipCount);
    return {};
}

} // namespace Frigate::Extract
