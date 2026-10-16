#include "extract/Inflate.h"
#include "extract/TexturePackers.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace Frigate::Extract {

namespace {

constexpr const char* kLogoPath = "rarewarelogo";
constexpr const char* kEyeIntroSightPath = "eyeintro/sight";
constexpr const char* kEyeIntroBloodPath = "eyeintro/blood";

// The logo segment opens with two light records (ambient, directional colour) and an empty list; a mismatch means
// the ROM is not the release the layout data describes.
constexpr std::array<uint8_t, 32> kLogoSignature = {
    0xdc, 0xdc, 0xdc, 0x00, 0xdc, 0xdc, 0xdc, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00,
    0x00, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// The 32x32 RGBA16 images the screen loads from its own code.
constexpr uint32_t kCodeImageWidth = 32;
constexpr uint32_t kCodeImageTexels = 32 * 32;
constexpr uint32_t kN64FormatRgba = 0;
constexpr uint32_t kN64FormatI = 4;
constexpr uint32_t kN64Size8b = 1;
constexpr uint32_t kN64Size16b = 2;

constexpr uint32_t kImageResourceVersion = 1;

// The blood stream's first bytes, to tell the right release.
constexpr std::array<uint8_t, 12> kBloodSignature = { 0x00, 0x20, 0x03, 0x05, 0x04, 0x02,
                                                      0x60, 0x21, 0x22, 0x01, 0xe0, 0xe0 };
// The backdrop: a big-endian u16 width and height, six more header bytes, then (count, value) byte pairs.
constexpr size_t kRleHeaderSize = 10;

Error RequireKnownRegion(const StageContext& context) {
    if (!context.layout().constants.known) {
        return Error::Make(ErrorCode::RegionUnsupported,
                           std::string("The ") + GetRegionInfo(context.region()).name +
                               " ROM's Rareware logo and gun barrel data are not mapped yet.");
    }
    return {};
}

std::string Hex6(uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%06x", value);
    return text;
}

Error NoTextureIds(uint32_t, TextureBinding&) {
    return Error::Make(ErrorCode::Corrupt, "The Rareware logo's lists load images by address, not by id.");
}

} // namespace

Error PackRarewareLogo(StageContext& context, O2rWriter& writer) {
    if (Error err = RequireKnownRegion(context); !err.ok()) {
        return err;
    }
    const RegionConstants& constants = context.layout().constants;
    const TextureConstants& layout = constants.textures;

    std::span<const uint8_t> segment;
    if (Error err = context.RomRange(constants.rarewareLogoRomStart,
                                     constants.rarewareLogoRomEnd - constants.rarewareLogoRomStart, segment);
        !err.ok()) {
        return err;
    }
    if (segment.size() < kLogoSignature.size() ||
        !std::equal(kLogoSignature.begin(), kLogoSignature.end(), segment.begin())) {
        return Error::Make(ErrorCode::Corrupt, "The ROM holds no Rareware logo segment where expected.");
    }

    Resolve resolve = [&](uint32_t address) -> std::optional<uint32_t> {
        if ((address >> 24) != (constants.rarewareLogoSegmentAddress >> 24)) {
            return std::nullopt;
        }
        uint32_t offset = address & 0x00FFFFFF;
        return offset < segment.size() ? std::optional<uint32_t>(offset) : std::nullopt;
    };

    std::vector<uint32_t> roots(std::begin(layout.rarewareLogoLists), std::end(layout.rarewareLogoLists));
    DisplayListRefs refs;
    for (uint32_t root : roots) {
        DisplayListRefs more = WalkDisplayList(segment, root, resolve);
        if (!more.unresolved.empty()) {
            return Error::Make(ErrorCode::Corrupt, "The Rareware logo has a list with unresolved references.");
        }
        refs.vertices.insert(refs.vertices.end(), more.vertices.begin(), more.vertices.end());
    }
    auto vertexRanges = MergeVertexRanges(refs);

    ReemittedLists emitted;
    if (Error err = ReemitSegmentLists(kLogoPath, segment, roots, resolve, vertexRanges, NoTextureIds, emitted);
        !err.ok()) {
        return err;
    }

    std::vector<uint8_t> payload;
    for (const auto& [begin, length] : vertexRanges) {
        if (uint64_t(begin) + length > segment.size()) {
            return Error::Make(ErrorCode::Corrupt, "A vertex array of the Rareware logo runs off its segment.");
        }
        if (Error err = EncodeVertexPayload(segment.subspan(begin, length), payload); !err.ok()) {
            return err;
        }
        if (Error err = writer.AddResource(std::string(kLogoPath) + "/vtx/" + Hex6(begin), kVertexResourceType,
                                           kVertexResourceVersion, 0, payload);
            !err.ok()) {
            return err;
        }
    }
    for (const EmittedList& list : emitted.lists) {
        if (Error err =
                writer.AddResource(list.path, kDisplayListResourceType, kDisplayListResourceVersion, 0, list.payload);
            !err.ok()) {
            return err;
        }
    }

    for (uint32_t offset : layout.rarewareLogoCodeImages) {
        const uint32_t length = kCodeImageTexels * 2;
        if (uint64_t(offset) + length > segment.size()) {
            return Error::Make(ErrorCode::Corrupt, "An image of the Rareware logo runs off its segment.");
        }
        EmbeddedImage image;
        image.path = std::string(kLogoPath) + "/img/" + Hex6(offset);
        image.offset = offset;
        image.fmt = kN64FormatRgba;
        image.size = kN64Size16b;
        image.width = kCodeImageWidth;
        image.texels = kCodeImageTexels;
        image.data.assign(segment.begin() + offset, segment.begin() + offset + length);
        emitted.SetImage(std::move(image));
    }
    for (const EmbeddedImage& image : emitted.images) {
        if (Error err = EncodeEmbeddedImagePayload(image, payload); !err.ok()) {
            return err;
        }
        if (Error err = writer.AddResource(image.path, "OTEX", kImageResourceVersion, 0, payload); !err.ok()) {
            return err;
        }
    }
    return {};
}

Error PackEyeIntro(StageContext& context, O2rWriter& writer) {
    if (Error err = RequireKnownRegion(context); !err.ok()) {
        return err;
    }
    const RegionConstants& constants = context.layout().constants;
    const TextureConstants& layout = constants.textures;

    // The backdrop: an RLE-compressed 8-bit intensity image in a fixed ROM region.
    std::span<const uint8_t> packed;
    if (Error err = context.RomRange(constants.eyeIntroSightRomStart, constants.eyeIntroSightRomSize, packed);
        !err.ok()) {
        return err;
    }
    auto be16 = [&](size_t at) { return (uint32_t(packed[at]) << 8) | packed[at + 1]; };
    if (packed.size() < kRleHeaderSize || be16(0) != layout.eyeIntroSightWidth ||
        be16(2) != layout.eyeIntroSightHeight) {
        return Error::Make(ErrorCode::Corrupt, "The ROM holds no gun barrel backdrop where expected.");
    }
    const size_t pixels = size_t(layout.eyeIntroSightWidth) * layout.eyeIntroSightHeight;
    std::vector<uint8_t> sight;
    sight.reserve(pixels + 255);
    for (size_t position = kRleHeaderSize; sight.size() < pixels; position += 2) {
        if (position + 2 > packed.size()) {
            return Error::Make(ErrorCode::Corrupt, "The gun barrel backdrop's run-length stream ends early.");
        }
        sight.insert(sight.end(), packed[position], packed[position + 1]);
    }
    sight.resize(pixels);

    // The blood animation stream: initialised data in the game code segment, which the ROM stores compressed.
    std::span<const uint8_t> compressed;
    if (Error err =
            context.RomRange(constants.eyeIntroGameSegmentRomStart, layout.eyeIntroGameSegmentRomSize, compressed);
        !err.ok()) {
        return err;
    }
    if (compressed.size() < 2 || compressed[0] != 0x11 || compressed[1] != 0x72) {
        return Error::Make(ErrorCode::Corrupt, "The ROM holds no game code segment where expected.");
    }
    std::vector<uint8_t> segment;
    if (Error err = InflateRaw(compressed.subspan(2), segment); !err.ok()) {
        return err;
    }
    if (segment.size() < size_t(layout.eyeIntroBloodOffset) + layout.eyeIntroBloodSize ||
        !std::equal(kBloodSignature.begin(), kBloodSignature.end(), segment.begin() + layout.eyeIntroBloodOffset)) {
        return Error::Make(ErrorCode::Corrupt, "The game code segment holds no blood animation stream.");
    }

    EmbeddedImage image;
    image.path = kEyeIntroSightPath;
    image.fmt = kN64FormatI;
    image.size = kN64Size8b;
    image.width = layout.eyeIntroSightWidth;
    image.texels = uint32_t(sight.size());
    image.data = std::move(sight);
    std::vector<uint8_t> payload;
    if (Error err = EncodeEmbeddedImagePayload(image, payload); !err.ok()) {
        return err;
    }
    if (Error err = writer.AddResource(kEyeIntroSightPath, "OTEX", kImageResourceVersion, 0, payload); !err.ok()) {
        return err;
    }
    return writer.AddResource(
        kEyeIntroBloodPath, "OBLB", 0, 0,
        std::span<const uint8_t>(segment.data() + layout.eyeIntroBloodOffset, layout.eyeIntroBloodSize));
}

} // namespace Frigate::Extract
