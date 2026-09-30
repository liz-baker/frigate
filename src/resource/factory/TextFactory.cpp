#include "resource/factory/TextFactory.h"
#include "resource/type/Text.h"

namespace Frigate {

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryTextV0::ReadResource(std::shared_ptr<Ship::File> file,
                                          std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto text = std::make_shared<Text>(initData);
    // The (initData) constructor is Resource's inherited one, not Text()'s
    // default ctor -- resize explicitly rather than relying on which
    // constructor ran.
    text->Strings.resize(Text::kOffsetTableSize);
    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);

    // Offsets are file-relative from the segment's own start -- capture that
    // absolute position before reading anything (see resource/type/Text.h).
    uint32_t bankStart = reader->GetBaseAddress();

    // A bank with fewer strings has a shorter table: the first string starts
    // where the table ends.
    std::vector<uint32_t> offsets(Text::kOffsetTableSize);
    uint32_t tableEnd = Text::kOffsetTableSize * sizeof(uint32_t);
    for (size_t i = 0; i * sizeof(uint32_t) < tableEnd; i++) {
        offsets[i] = reader->ReadUInt32();
        if (offsets[i] != 0 && offsets[i] < tableEnd) {
            tableEnd = offsets[i];
        }
    }

    for (size_t i = 0; i < Text::kOffsetTableSize; i++) {
        if (offsets[i] == 0) {
            continue;
        }
        reader->Seek(static_cast<int32_t>(bankStart + offsets[i]), Ship::SeekOffsetType::Start);

        // BinaryReader::ReadCString()'s own doc comment says the null
        // terminator is "consumed but not included in the returned string"
        // -- its actual implementation appends it before checking for it,
        // so every result carries a trailing '\0' contrary to that contract.
        // Strip it here rather than let every Strings[] entry silently be
        // one byte longer than it looks (breaks == against a C string,
        // length checks, etc.).
        std::string value = reader->ReadCString();
        if (!value.empty() && value.back() == '\0') {
            value.pop_back();
        }
        text->Strings[i] = std::move(value);
    }

    return text;
}

} // namespace Frigate
