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

    // Whatever's left in the segment is the raw glyph pixel-byte blob (see
    // resource/type/Font.h's module comment -- pixel format not decoded here).
    size_t remaining = file->Buffer->size() - reader->GetBaseAddress();
    font->PixelData.resize(remaining);
    if (remaining > 0) {
        reader->Read(reinterpret_cast<char*>(font->PixelData.data()), static_cast<int32_t>(remaining));
    }

    return font;
}

} // namespace Frigate
