#include "resource/factory/StanFactory.h"
#include "resource/type/Stan.h"

namespace Frigate {

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryStanV0::ReadResource(std::shared_ptr<Ship::File> file,
                                          std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);
    auto stan = std::make_shared<StanResource>(initData);

    const size_t start = reader->GetBaseAddress();
    const auto* bytes = reinterpret_cast<const uint8_t*>(file->Buffer->data()) + start;
    if (!Level::ParseStanPayload(bytes, file->Buffer->size() - start, &stan->Data)) {
        return nullptr;
    }
    return stan;
}

} // namespace Frigate
