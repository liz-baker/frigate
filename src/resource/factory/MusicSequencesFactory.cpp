#include "resource/factory/MusicSequencesFactory.h"
#include "resource/type/MusicSequencesResource.h"

namespace Frigate {

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryMusicSequencesV0::ReadResource(std::shared_ptr<Ship::File> file,
                                                    std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);
    auto sequences = std::make_shared<MusicSequencesResource>(initData);

    const size_t start = reader->GetBaseAddress();
    const auto* bytes = reinterpret_cast<const uint8_t*>(file->Buffer->data()) + start;
    if (!Audio::ParseMusicSequencesPayload(bytes, file->Buffer->size() - start, &sequences->Data)) {
        return nullptr;
    }
    return sequences;
}

} // namespace Frigate
