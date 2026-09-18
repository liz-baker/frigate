#include "resource/factory/FontFactory.h"
#include "resource/type/Font.h"

namespace Frigate {

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryFontV0::ReadResource(std::shared_ptr<Ship::File> file,
                                          std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto font = std::make_shared<Font>(initData);
    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);

    for (size_t i = 0; i < Font::kKerningTableSize; i++) {
        font->Kerning[i] = reader->ReadInt32();
    }

    font->Chars.reserve(Font::kNumChars);
    for (size_t i = 0; i < Font::kNumChars; i++) {
        FontChar fontChar;
        fontChar.index = reader->ReadInt32();
        fontChar.baseline = reader->ReadInt32();
        fontChar.height = reader->ReadInt32();
        fontChar.width = reader->ReadInt32();
        fontChar.kerningIndex = reader->ReadInt32();
        fontChar.pixelDataOffset = reader->ReadUInt32();
        font->Chars.push_back(fontChar);
    }

    const auto& segment = *file->Buffer;
    for (auto& fontChar : font->Chars) {
        if (fontChar.width < 0 || fontChar.height < 0) {
            return nullptr;
        }
        const size_t size = static_cast<size_t>(FontChar::RowStride(fontChar.width)) * fontChar.height;
        if (fontChar.pixelDataOffset > segment.size() || size > segment.size() - fontChar.pixelDataOffset) {
            return nullptr;
        }
        const auto* src = reinterpret_cast<const uint8_t*>(segment.data()) + fontChar.pixelDataOffset;
        fontChar.Pixels.assign(src, src + size);
    }

    return font;
}

} // namespace Frigate
