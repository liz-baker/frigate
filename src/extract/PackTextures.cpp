#include "extract/ImageDefs.h"
#include "extract/Texture.h"
#include "extract/TextureMip.h"
#include "extract/TexturePackers.h"

#include <string>

namespace Frigate::Extract {

namespace {

constexpr const char* kTexturePrefix = "textures";
constexpr uint32_t kTextureResourceVersion = 1;
// libultraship's CI8 import reads a full 256-entry table, so a shorter CI8 palette is zero-padded; indices past the
// real count never occur.
constexpr size_t kCi8PaletteEntries = 256;

// The N64 (format, pixel size) G_SETTIMG must say for libultraship to decode a decoded texture's bytes.
// Formats: 0 RGBA, 2 CI, 3 IA, 4 I. Sizes: 0=4b, 1=8b, 2=16b, 3=32b.
void N64FormatOf(TexFormat format, uint32_t& fmt, uint32_t& size) {
    switch (format) {
        case TexFormat::RGBA32:
        case TexFormat::RGB24:
            fmt = 0, size = 3;
            return;
        case TexFormat::RGBA16:
        case TexFormat::RGB15:
            fmt = 0, size = 2;
            return;
        case TexFormat::IA16:
            fmt = 3, size = 2;
            return;
        case TexFormat::IA8:
            fmt = 3, size = 1;
            return;
        case TexFormat::IA4:
            fmt = 3, size = 0;
            return;
        case TexFormat::I8:
            fmt = 4, size = 1;
            return;
        case TexFormat::I4:
            fmt = 4, size = 0;
            return;
        case TexFormat::RGBA16_CI8:
        case TexFormat::IA16_CI8:
            fmt = 2, size = 1;
            return;
        case TexFormat::RGBA16_CI4:
        case TexFormat::IA16_CI4:
            fmt = 2, size = 0;
            return;
    }
}

Error WriteTexture(O2rWriter& writer, const std::string& path, TextureType type, uint32_t width, uint32_t height,
                   std::span<const uint8_t> pixels) {
    std::vector<uint8_t> payload = EncodeTexturePayload(uint32_t(type), width, height, pixels);
    // Id 0: the resource loader needs ids unique only if used.
    return writer.AddResource(path, "OTEX", kTextureResourceVersion, 0, payload);
}

} // namespace

Error ReadImageBytes(const StageContext& context, size_t index, std::span<const uint8_t>& bytes) {
    std::span<const ImageDef> defs = ImageDefs();
    std::span<const ImageEntry> table = ImageTable();
    if (index >= defs.size() || index >= table.size()) {
        return Error::Make(ErrorCode::Corrupt, "A display list names a texture that is not in the texture bank.");
    }
    return context.RomRange(ImageRomOffset(context.region(), index), table[index].size, bytes);
}

Error TextureBindings::Lookup(uint32_t textureId, TextureBinding& binding) {
    auto cached = mCache.find(textureId);
    if (cached != mCache.end()) {
        binding = cached->second;
        return {};
    }
    std::span<const uint8_t> bytes;
    if (Error err = ReadImageBytes(mContext, textureId, bytes); !err.ok()) {
        return err;
    }
    TextureHeader header;
    if (Error err = ReadTextureHeader(bytes, header); !err.ok()) {
        return err;
    }
    const ImageDef& def = ImageDefs()[textureId];
    TextureBinding made;
    made.path = std::string(kTexturePrefix) + "/" + def.name;
    N64FormatOf(header.format, made.fmt, made.size);
    made.width = header.width;
    if (IsPaletted(header.format)) {
        made.palettePath = made.path + ".pal";
    }
    made.detail = def.PackedDetail();
    mCache[textureId] = made;
    binding = std::move(made);
    return {};
}

Error PackTextures(StageContext& context, O2rWriter& writer) {
    std::span<const ImageDef> defs = ImageDefs();
    DecodedTexture texture;
    std::vector<uint8_t> palette;
    std::vector<uint8_t> chainBytes;

    for (size_t i = 0; i < defs.size(); i++) {
        if (context.Cancelled()) {
            return Error::Make(ErrorCode::Cancelled, "Extraction was cancelled.");
        }
        context.Report(i, defs.size(), defs[i].name);

        std::span<const uint8_t> bytes;
        if (Error err = ReadImageBytes(context, i, bytes); !err.ok()) {
            return err;
        }
        if (Error err = DecodeTexture(bytes, texture); !err.ok()) {
            return Error::Make(err.code, std::string(defs[i].name) + ": " + err.message);
        }
        const std::string path = std::string(kTexturePrefix) + "/" + defs[i].name;
        const TextureHeader& header = texture.header;

        if (Error err = WriteTexture(writer, path, texture.textureType, header.width, header.height, texture.pixels);
            !err.ok()) {
            return err;
        }

        if (header.paletted) {
            palette.clear();
            for (uint16_t word : texture.paletteRgba5551) {
                palette.push_back(uint8_t(word >> 8));
                palette.push_back(uint8_t(word));
            }
            if (texture.textureType == TextureType::Palette8bpp && palette.size() < kCi8PaletteEntries * 2) {
                palette.resize(kCi8PaletteEntries * 2, 0);
            }
            if (Error err = WriteTexture(writer, path + ".pal", TextureType::RGBA16bpp, uint32_t(palette.size() / 2), 1,
                                         palette);
                !err.ok()) {
                return err;
            }
        }

        if (std::optional<std::vector<MipLevel>> chain = BuildMipChain(texture)) {
            chainBytes.clear();
            for (const MipLevel& level : *chain) {
                chainBytes.insert(chainBytes.end(), level.data.begin(), level.data.end());
            }
            if (Error err =
                    WriteTexture(writer, path + ".mip", texture.textureType, header.width, header.height, chainBytes);
                !err.ok()) {
                return err;
            }
        }
    }
    context.Report(defs.size(), defs.size());
    return {};
}

} // namespace Frigate::Extract
