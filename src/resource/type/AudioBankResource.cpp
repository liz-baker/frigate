#include "resource/type/AudioBankResource.h"

namespace Frigate {

AudioBankResource::AudioBankResource() : Resource(std::shared_ptr<Ship::ResourceInitData>()) {
}

Audio::AudioBank* AudioBankResource::GetPointer() {
    return &Data;
}

size_t AudioBankResource::GetPointerSize() {
    return Data.samples.size();
}

} // namespace Frigate
