#include "extract/AnimationClip.h"
#include "extract/ModelContainer.h"
#include "extract/ModelPackers.h"
#include "extract/PackCommon.h"
#include "extract/Stan.h"
#include "extract/TexturePackers.h"

#include <algorithm>
#include <array>
#include <map>
#include <string>

namespace Frigate::Extract {

namespace {

// ROM files under the bg directory, as `<stage>_all_p.bin`.
constexpr std::string_view kBgPrefix = "assets/obseg/bg/";
constexpr std::string_view kBgSuffix = "_all_p.bin";

constexpr const char* kBlobResourceType = "OBLB";

// Citadel's stan file is in the beta layout (float points, tiles named by pointer), which the game's stan code cannot
// read and whose stage cannot be loaded; it stays an opaque blob.
constexpr std::string_view kBetaStanFile = "Tbg_cat_all_p_stanZ";

struct LevelCategory {
    std::string_view prefix;
    std::string_view directory; // under `obseg/`; empty for fonts
    const char* type;           // the resource type of the packed payload
};

// stan is decoded into GEST; text and font use the GETX and GEFN types so their factories pick them up.
constexpr LevelCategory kLevelCategories[] = {
    { "assets/obseg/stan/", "stan", "GEST" },
    { "assets/obseg/text/", "text", "GETX" },
    { "assets/obseg/setup/", "setup", "OBLB" },
    { "assets/obseg/brief/", "brief", "OBLB" },
    { "assets/font/", "", "GEFN" },
};

// A font bank's kerning table and char table + pixel data are two files in the file list but one contiguous segment
// in the ROM: pixelDataOffset is relative to the combined segment, so the resource is the two files in this order.
constexpr std::array<std::string_view, 2> kFontParts = { "_kerning", "_fontchartable" };

// The file's bytes, without a copy when it is stored uncompressed.
Error FileBytes(const StageContext& context, const FileEntry& entry, std::vector<uint8_t>& scratch,
                std::span<const uint8_t>& bytes) {
    if (!entry.compressed) {
        return context.RomRange(entry.offset, entry.size, bytes);
    }
    if (Error err = context.ReadFile(entry, scratch); !err.ok()) {
        return err;
    }
    bytes = scratch;
    return {};
}

Error PackRoom(const BgRoom& room, const std::string& base, const TextureLookup& textures, O2rWriter& writer) {
    // The vertex ranges the room's lists load, from the primary and then the secondary list.
    DisplayListRefs combined;
    Resolve resolve = [&room](uint32_t address) { return room.Resolve(address); };
    struct Blob {
        const char* kind;
        const std::vector<uint8_t>* data;
    };
    std::vector<Blob> blobs = { { "primary", &room.primary } };
    if (room.hasSecondary) {
        blobs.push_back({ "secondary", &room.secondary });
    }
    for (const Blob& blob : blobs) {
        DisplayListRefs refs = WalkDisplayList(*blob.data, 0, resolve);
        if (!refs.unresolved.empty()) {
            return Error::Make(ErrorCode::Corrupt, "A level room's display list has unresolved references.");
        }
        combined.vertices.insert(combined.vertices.end(), refs.vertices.begin(), refs.vertices.end());
    }
    std::vector<std::pair<uint32_t, uint32_t>> ranges = MergeVertexRanges(combined);

    std::vector<uint8_t> scratch;
    for (const auto& [begin, length] : ranges) {
        if (uint64_t(begin) + length > room.vertices.size()) {
            return Error::Make(ErrorCode::Corrupt, "A level room's vertex range runs off its vertex blob.");
        }
        if (Error err = WriteVertexBlock(writer, base + "/vtx/" + Hex6(begin),
                                         std::span<const uint8_t>(room.vertices).subspan(begin, length), scratch);
            !err.ok()) {
            return err;
        }
    }

    auto vertexTarget = [&](const VertexRef& ref, std::string& path, uint32_t& byteOffset) -> Error {
        for (const auto& [begin, length] : ranges) {
            if (begin <= ref.offset && uint64_t(ref.offset) + ref.count * kVertexSize <= uint64_t(begin) + length) {
                path = base + "/vtx/" + Hex6(begin);
                byteOffset = ref.offset - begin;
                return {};
            }
        }
        return Error::Make(ErrorCode::Corrupt, "A level room's G_VTX is in no vertex block.");
    };
    auto noImages = [](uint32_t) -> std::string { return {}; };

    for (const Blob& blob : blobs) {
        ReemitSpec spec;
        spec.what = base + " " + blob.kind;
        spec.data = *blob.data;
        spec.roots = { 0 };
        spec.resolve = resolve;
        std::string kind = blob.kind;
        spec.listPath = [&base, kind](uint32_t offset) { return base + "/dl/" + kind + "/" + Hex6(offset); };
        // Room lists load textures by id; an embedded image there would be a bug in the walk.
        spec.imagePath = noImages;
        spec.vertexTarget = vertexTarget;
        spec.textures = &textures;
        ReemittedLists lists;
        if (Error err = ReemitLists(spec, lists); !err.ok()) {
            return err;
        }
        if (!lists.images.empty()) {
            return Error::Make(ErrorCode::Corrupt, "A level room's list loads an image from its own data.");
        }
        if (Error err = WriteLists(writer, lists); !err.ok()) {
            return err;
        }
    }
    return {};
}

// The commands of a water list, as the game's F3D-numbered microcode reads them. The game rewrites the two tile
// sizes (commands 2 and 3) and the primitive colour (command 4). Layout: docs/formats/goldeneye-water-lists.md.
using Command = std::array<uint32_t, 2>;

// gsDPSetTile(fmt, siz, line, 0, tile, 0, WRAP, 5 or 6, 0, WRAP, 5 or 6, 0).
constexpr Command kSetTileCaverns[] = { { 0xF5800800, 0x00018060 }, { 0xF5800800, 0x01018060 } };
constexpr Command kSetTileComplex[] = { { 0xF5480400, 0x00014050 }, { 0xF5480400, 0x01014050 } };
// gsDPSetTileSize(tile, 2, 2, 0, 0) twice, gsDPSetPrimColor(0, 15, 255, 255, 255, 255),
// gsDPSetTextureDetail(G_TD_CLAMP), gsDPSetTextureFilter(G_TF_BILERP), gsDPSetCombineLERP(TEXEL1, TEXEL0,
// PRIM_LOD_FRAC, TEXEL0, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0),
// gsDPSetRenderMode(G_RM_PASS, G_RM_AA_ZB_OPA_SURF2), gsDPSetTextureLOD(G_TL_TILE), gsDPSetCycleType(G_CYC_2CYCLE),
// gsSPSetGeometryMode(G_CULL_BACK), gsSPEndDisplayList().
constexpr Command kWaterCommon[] = {
    { 0xF2002002, 0x00000000 }, { 0xF2002002, 0x01000000 }, { 0xFA00000F, 0xFFFFFFFF }, { 0xBA001102, 0x00000000 },
    { 0xBA000C02, 0x00002000 }, { 0xFC272C04, 0x1F1093FF }, { 0xB900031D, 0x0C192078 }, { 0xBA001001, 0x00000000 },
    { 0xBA001402, 0x00100000 }, { 0xB7000000, 0x00002000 }, { 0xB8000000, 0x00000000 },
};

// The images the game follows with a call to a list that scrolls them (texLoadFromGdl): the Caverns water, and the
// water in Dam and Complex.
constexpr uint32_t kWaterTextures[] = { 1508, 1511 };

std::vector<uint8_t> EncodeWaterList(const Command (&tiles)[2]) {
    std::vector<uint8_t> out(kGfxSize, 0);
    out[0] = kUcodeIndyGe;
    for (const Command& command : tiles) {
        AppendWord(out, command[0]);
        AppendWord(out, command[1]);
    }
    for (const Command& command : kWaterCommon) {
        AppendWord(out, command[0]);
        AppendWord(out, command[1]);
    }
    return out;
}

} // namespace

Error PackBgRooms(StageContext& context, O2rWriter& writer) {
    if (Error err = RequireKnownRegion(context, "level geometry"); !err.ok()) {
        return err;
    }

    std::vector<const FileEntry*> files;
    for (const FileEntry& entry : context.layout().files) {
        std::string_view name = entry.name;
        if (StartsWith(name, kBgPrefix) && name.size() >= kBgSuffix.size() &&
            name.substr(name.size() - kBgSuffix.size()) == kBgSuffix) {
            files.push_back(&entry);
        }
    }

    TextureBindings bindings(context);
    TextureLookup textures = bindings.AsLookup();
    std::vector<uint8_t> scratch;
    for (size_t i = 0; i < files.size(); i++) {
        if (context.Cancelled()) {
            return Error::Make(ErrorCode::Cancelled, "Cancelled.");
        }
        const FileEntry& entry = *files[i];
        std::string_view file = BaseName(entry.name);
        std::string stage(file.substr(0, file.size() - kBgSuffix.size()));
        std::string stageBase = "bg/" + stage;
        context.Report(i, files.size(), stageBase);

        std::span<const uint8_t> bytes;
        if (Error err = FileBytes(context, entry, scratch, bytes); !err.ok()) {
            return err;
        }
        BgFile bg;
        // A file with no rooms is an empty placeholder and has nothing to write.
        if (!BgFile::Parse(bytes, bg).ok()) {
            continue;
        }
        if (Error err = writer.AddResource(stageBase + "/header", kBlobResourceType, 0, 0, bg.header); !err.ok()) {
            return err;
        }
        char room[16];
        BgRoom decoded;
        for (size_t n = 0; n < bg.rooms.size(); n++) {
            if (Error err = bg.InflateRoom(n, decoded); !err.ok()) {
                return err;
            }
            std::snprintf(room, sizeof(room), "room%02u", decoded.index);
            if (Error err = PackRoom(decoded, stageBase + "/" + room, textures, writer); !err.ok()) {
                return err;
            }
        }
    }
    context.Report(files.size(), files.size());
    return {};
}

Error PackLevelData(StageContext& context, O2rWriter& writer) {
    if (Error err = RequireKnownRegion(context, "level data"); !err.ok()) {
        return err;
    }

    std::vector<uint8_t> scratch, packed;
    // Font banks in the order the file list first names them, each with its parts' entries.
    std::vector<std::string> bankOrder;
    std::map<std::string, std::array<const FileEntry*, 2>> banks;

    for (const FileEntry& entry : context.layout().files) {
        if (context.Cancelled()) {
            return Error::Make(ErrorCode::Cancelled, "Cancelled.");
        }
        std::string_view name = entry.name;
        const LevelCategory* category = nullptr;
        for (const LevelCategory& candidate : kLevelCategories) {
            if (StartsWith(name, candidate.prefix)) {
                category = &candidate;
                break;
            }
        }
        if (category == nullptr) {
            continue;
        }
        std::string_view virtualName = BaseName(name);
        if (virtualName.size() > 4 && virtualName.substr(virtualName.size() - 4) == ".bin") {
            virtualName.remove_suffix(4);
        }

        if (category->directory.empty()) {
            bool matched = false;
            for (size_t part = 0; part < kFontParts.size() && !matched; part++) {
                std::string_view suffix = kFontParts[part];
                if (virtualName.size() > suffix.size() &&
                    virtualName.substr(virtualName.size() - suffix.size()) == suffix) {
                    std::string bank(virtualName.substr(0, virtualName.size() - suffix.size()));
                    if (banks.count(bank) == 0) {
                        bankOrder.push_back(bank);
                    }
                    banks[bank][part] = &entry;
                    matched = true;
                }
            }
            if (!matched) {
                return Error::Make(ErrorCode::Corrupt,
                                   "A font file has an unrecognized name: " + std::string(virtualName) + ".");
            }
            continue;
        }

        std::span<const uint8_t> bytes;
        if (Error err = FileBytes(context, entry, scratch, bytes); !err.ok()) {
            return err;
        }
        std::string path = "obseg/" + std::string(category->directory) + "/" + std::string(virtualName);
        const char* type = category->type;
        uint32_t version = 0;
        if (category->directory == "stan") {
            if (virtualName == kBetaStanFile) {
                type = kBlobResourceType;
                version = 1;
            } else {
                if (Error err = ConvertStanFile(bytes, packed); !err.ok()) {
                    return err;
                }
                bytes = packed;
            }
        }
        if (Error err = writer.AddResource(path, type, version, 0, bytes); !err.ok()) {
            return err;
        }
    }

    for (const std::string& bank : bankOrder) {
        const auto& parts = banks[bank];
        if (parts[0] == nullptr || parts[1] == nullptr) {
            continue;
        }
        std::vector<uint8_t> payload;
        for (const FileEntry* part : parts) {
            std::span<const uint8_t> bytes;
            if (Error err = FileBytes(context, *part, scratch, bytes); !err.ok()) {
                return err;
            }
            payload.insert(payload.end(), bytes.begin(), bytes.end());
        }
        if (Error err = writer.AddResource("font/" + bank, "GEFN", 0, 0, payload); !err.ok()) {
            return err;
        }
    }
    return {};
}

Error PackWaterLists(StageContext&, O2rWriter& writer) {
    for (uint32_t texture : kWaterTextures) {
        std::vector<uint8_t> payload =
            EncodeWaterList(texture == kWaterTextures[0] ? kSetTileCaverns : kSetTileComplex);
        if (Error err = writer.AddResource("bg/water/" + std::to_string(texture), kDisplayListResourceType,
                                           kDisplayListResourceVersion, 0, payload);
            !err.ok()) {
            return err;
        }
    }
    return {};
}

void RegisterModelPackers(Extractor& extractor) {
    // The Objects archive's other packers are registered with their own ports.
    extractor.RegisterStage(MakeStage("Models", ArchiveId::Objects, PackModels));
    extractor.RegisterStage(MakeStage("Animation clips", ArchiveId::Objects, PackAnimationClips));
    extractor.RegisterStage(MakeStage("Level data", ArchiveId::Geometry, PackLevelData));
    extractor.RegisterStage(MakeStage("Level geometry", ArchiveId::Geometry, PackBgRooms));
    extractor.RegisterStage(MakeStage("Water lists", ArchiveId::Geometry, PackWaterLists));
}

} // namespace Frigate::Extract
