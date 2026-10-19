#include "extract/ImageDefs.h"
#include "extract/TexturePackers.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace Frigate::Extract {

namespace {

constexpr const char* kHudPath = "hud/img";
constexpr const char* kEffectPath = "hud/fx";
constexpr const char* kGlobalListPath = "hud/gdl";
constexpr const char* kHudTablePath = "hud/images";
constexpr const char* kSkyPath = "sky/img";
constexpr const char* kHitsPath = "textures/hits";

// A request's tile 1: the image is loaded at the TMEM word address in bits 3 to 11 and sets tile 1, whose s and t
// coordinate shifts are the 3-bit fields at bits 12 and 15 (src/texture/TexExpand.cpp,
// docs/formats/goldeneye-display-list-resource.md).
constexpr uint32_t kRequestTypeSecondTile = 5;
constexpr uint32_t kRequestTmemShift = 3;
constexpr uint32_t kRequestShiftS = 12;
constexpr uint32_t kRequestShiftT = 15;

// A global list's image load is marked by this in the top half of its G_SETTIMG address.
constexpr uint32_t kImageMarker = 0xABCD;

Error Corrupt(const std::string& what) {
    return Error::Make(ErrorCode::Corrupt, what);
}

Error RequireKnownRegion(const StageContext& context) {
    if (!context.layout().constants.known) {
        return Error::Make(ErrorCode::RegionUnsupported, std::string("The ") + GetRegionInfo(context.region()).name +
                                                             " ROM's image tables are not mapped yet.");
    }
    return {};
}

std::string Hex4(uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%04x", value);
    return text;
}

// The ROM's big-endian words, with a bounds check the Python reads get from struct.
Error ReadWord(std::span<const uint8_t> rom, uint64_t at, uint32_t& word) {
    if (at + 4 > rom.size()) {
        return Corrupt("A display list in the ROM runs off its end.");
    }
    word = (uint32_t(rom[at]) << 24) | (uint32_t(rom[at + 1]) << 16) | (uint32_t(rom[at + 2]) << 8) | rom[at + 3];
    return {};
}

struct TableEntry {
    uint32_t offset;
    uint32_t textureId;
    uint32_t width, height, level;
    uint32_t fmt;
    uint32_t sFlags, tFlags;
    bool effect;
};

Error ReadTableEntry(std::span<const uint8_t> rom, uint64_t tableStart, uint32_t offset, TableEntry& entry) {
    uint64_t at = tableStart + offset;
    if (at + kImageTableEntrySize > rom.size()) {
        return Corrupt("The ROM is too small to hold the global image table.");
    }
    entry.offset = offset;
    entry.textureId =
        (uint32_t(rom[at]) << 24) | (uint32_t(rom[at + 1]) << 16) | (uint32_t(rom[at + 2]) << 8) | rom[at + 3];
    entry.width = rom[at + 4];
    entry.height = rom[at + 5];
    entry.level = rom[at + 6];
    entry.fmt = rom[at + 7];
    // rom[at + 8] is the table's own size field, unused.
    entry.sFlags = rom[at + 9];
    entry.tFlags = rom[at + 10];
    return {};
}

Error AddList(O2rWriter& writer, const std::string& path, const std::vector<uint8_t>& payload) {
    return writer.AddResource(path, kDisplayListResourceType, kDisplayListResourceVersion, 0, payload);
}

// The request for the second image of a global list, from the commands that follow its load: the render tile (the
// one that is not tile 7) gives the TMEM address, the wrap modes and the coordinate shifts.
Error SecondImageRequest(std::span<const uint8_t> rom, uint64_t at, uint32_t& request) {
    for (;;) {
        uint32_t w0, w1;
        if (Error err = ReadWord(rom, at, w0); !err.ok()) {
            return err;
        }
        if (Error err = ReadWord(rom, at + 4, w1); !err.ok()) {
            return err;
        }
        at += kGfxSize;
        uint32_t opcode = w0 >> 24;
        if (opcode == kG_SETTILE && ((w1 >> 24) & 7) != 7) {
            uint32_t tmem = w0 & 0x1FF;
            uint32_t cmt = (w1 >> 18) & 3, cms = (w1 >> 8) & 3;
            uint32_t shiftT = (w1 >> 10) & 0xF, shiftS = w1 & 0xF;
            if (shiftT > 7 || shiftS > 7) {
                return Corrupt("A global display list shifts a second image's coordinates by more than 7.");
            }
            request = (kG_SETTEX << 24) | (RequestWrapMode(cms) << 22) | (RequestWrapMode(cmt) << 20) |
                      (shiftT << kRequestShiftT) | (shiftS << kRequestShiftS) | (tmem << kRequestTmemShift) |
                      kRequestTypeSecondTile;
            return {};
        }
        if (opcode == kG_SETTILESIZE || opcode == kG_ENDDL) {
            return Corrupt("A global display list loads a second image into no render tile.");
        }
    }
}

// A display list of the table that selects one image, or a smoke and a fire image: its commands as the ROM has them,
// each image's load (the texture, the tile that loads it and the one that draws it) replaced by the request
// texSelect would make. The first image is tile 0's; a second is tile 1's.
Error EncodeGlobalList(std::span<const uint8_t> rom, uint64_t listStart, TextureBindings& textures,
                       std::vector<uint8_t>& out) {
    out.assign(kGfxSize, 0);
    out[0] = kUcodeIndyGe;
    uint64_t at = listStart;
    int images = 0;
    bool skipping = false;
    for (;;) {
        uint32_t w0, w1;
        if (Error err = ReadWord(rom, at, w0); !err.ok()) {
            return err;
        }
        if (Error err = ReadWord(rom, at + 4, w1); !err.ok()) {
            return err;
        }
        at += kGfxSize;
        uint32_t opcode = w0 >> 24;
        if (opcode == kG_SETTIMG && (w1 >> 16) == kImageMarker && !skipping) {
            uint32_t textureId = w1 & 0xFFFF;
            TextureBinding binding;
            if (Error err = textures.Lookup(textureId, binding); !err.ok()) {
                return err;
            }
            uint32_t request;
            if (images == 0) {
                request = (kG_SETTEX << 24) | kRequestTypeTilePreswapped;
            } else if (images == 1) {
                if (Error err = SecondImageRequest(rom, at, request); !err.ok()) {
                    return err;
                }
            } else {
                return Corrupt("A global display list loads more than two images.");
            }
            AppendWord(out, request);
            AppendWord(out, textureId);
            AppendReferenceCommand(out, kOtrG_SETTIMG_HASH,
                                   (binding.fmt << 21) | (binding.size << 19) | (binding.width - 1), binding.path);
            images++;
            skipping = true;
            continue;
        }
        if (skipping) {
            // The load's commands, up to and including the render tile's size.
            if (opcode == kG_SETTILESIZE) {
                skipping = false;
            }
            continue;
        }
        AppendWord(out, w0);
        AppendWord(out, w1);
        if (opcode == kG_ENDDL) {
            break;
        }
    }
    if (images == 0) {
        return Corrupt("A global display list loads no image.");
    }
    return {};
}

} // namespace

Error PackTextureHits(StageContext& context, O2rWriter& writer) {
    std::string text;
    for (const ImageDef& def : ImageDefs()) {
        text += def.name;
        text += ' ';
        text += std::to_string(int(def.hitSound));
        text += ' ';
        text += std::to_string(int(def.hitImpact));
        text += '\n';
    }
    (void)context;
    return writer.AddResource(kHitsPath, "OBLB", 0, 0,
                              std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

Error PackHudImages(StageContext& context, O2rWriter& writer) {
    if (Error err = RequireKnownRegion(context); !err.ok()) {
        return err;
    }
    const RegionConstants& constants = context.layout().constants;
    const TextureConstants& layout = constants.textures;
    std::span<const uint8_t> rom = context.rom();
    TextureBindings textures(context);

    std::vector<TableEntry> entries;
    for (size_t r = 0; r < layout.imageTableRangeCount; r++) {
        const ImageTableRange& range = layout.imageTableRanges[r];
        for (uint32_t offset = range.first; offset < range.last; offset += kImageTableEntrySize) {
            TableEntry entry;
            if (Error err = ReadTableEntry(rom, constants.hudImageTableRomStart, offset, entry); !err.ok()) {
                return err;
            }
            if ((entry.level != 0 && !range.effect) || entry.width == 0 || entry.height == 0) {
                return Corrupt("A global image table entry is not a plain image.");
            }
            entry.effect = range.effect || offset == layout.hudDotOffset;
            entries.push_back(entry);
        }
    }

    // The sizes the HUD lays its images out with, one line per entry.
    std::string table;
    for (const TableEntry& entry : entries) {
        char line[64];
        std::snprintf(line, sizeof(line), "0x%x\t%u\t%u\t%u\n", entry.offset, entry.width, entry.height, entry.fmt);
        table += line;
    }
    if (Error err =
            writer.AddResource(kHudTablePath, "OBLB", 0, 0,
                               std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(table.data()), table.size()));
        !err.ok()) {
        return err;
    }

    for (const TableEntry& entry : entries) {
        TextureBinding binding;
        if (Error err = textures.Lookup(entry.textureId, binding); !err.ok()) {
            return err;
        }
        uint32_t request = (kG_SETTEX << 24) | (RequestWrapMode(entry.sFlags) << 22) |
                           (RequestWrapMode(entry.tFlags) << 20) | (entry.effect ? (kRequestSampleOffset << 18) : 0) |
                           kRequestTypeTilePreswapped;
        std::string path = std::string(entry.effect ? kEffectPath : kHudPath) + "/" + Hex4(entry.offset);
        if (Error err = AddList(writer, path, EncodeTextureRequestList(request, entry.textureId, binding)); !err.ok()) {
            return err;
        }
    }

    std::vector<uint32_t> globalLists = { layout.singleImageLists[0], layout.singleImageLists[1] };
    for (uint32_t frame = 0; frame < layout.flareListCount; frame++) {
        globalLists.push_back(layout.flareListFirst + layout.flareListStride * frame);
    }
    std::vector<uint8_t> payload;
    for (uint32_t offset : globalLists) {
        if (Error err = EncodeGlobalList(rom, uint64_t(constants.hudImageTableRomStart) + offset, textures, payload);
            !err.ok()) {
            return err;
        }
        if (Error err = AddList(writer, std::string(kGlobalListPath) + "/" + Hex4(offset), payload); !err.ok()) {
            return err;
        }
    }
    return {};
}

Error PackSkyImages(StageContext& context, O2rWriter& writer) {
    if (Error err = RequireKnownRegion(context); !err.ok()) {
        return err;
    }
    const RegionConstants& constants = context.layout().constants;
    const TextureConstants& layout = constants.textures;
    TextureBindings textures(context);

    for (uint32_t index = 0; index < layout.skyEntryCount; index++) {
        TableEntry entry;
        uint32_t offset = layout.skyTableOffset + index * uint32_t(kImageTableEntrySize);
        if (Error err = ReadTableEntry(context.rom(), constants.hudImageTableRomStart, offset, entry); !err.ok()) {
            return err;
        }
        if (entry.width == 0 || entry.height == 0) {
            return Corrupt("A sky image has no size.");
        }
        TextureBinding binding;
        if (Error err = textures.Lookup(entry.textureId, binding); !err.ok()) {
            return err;
        }
        uint32_t kind = entry.level ? kRequestTypeMipmap : kRequestTypeTilePreswapped;
        uint32_t request = (kG_SETTEX << 24) | (RequestWrapMode(entry.sFlags) << 22) |
                           (RequestWrapMode(entry.tFlags) << 20) | (kRequestSampleOffset << 18) | kind;
        if (Error err = AddList(writer, std::string(kSkyPath) + "/" + std::to_string(index),
                                EncodeTextureRequestList(request, entry.textureId, binding));
            !err.ok()) {
            return err;
        }
    }
    return {};
}

} // namespace Frigate::Extract
