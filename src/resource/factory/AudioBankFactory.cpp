#include "resource/factory/AudioBankFactory.h"
#include "resource/type/AudioBankResource.h"

namespace Frigate {

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryAudioBankV0::ReadResource(std::shared_ptr<Ship::File> file,
                                               std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);
    auto bank = std::make_shared<AudioBankResource>(initData);

    const size_t start = reader->GetBaseAddress();
    const auto* bytes = reinterpret_cast<const uint8_t*>(file->Buffer->data()) + start;
    if (!Audio::ParseAudioBankPayload(bytes, file->Buffer->size() - start, &bank->Data)) {
        return nullptr;
    }
    return bank;
}

} // namespace Frigate
