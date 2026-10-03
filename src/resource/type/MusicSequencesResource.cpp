#include "resource/type/MusicSequencesResource.h"

namespace Frigate {

MusicSequencesResource::MusicSequencesResource() : Resource(std::shared_ptr<Ship::ResourceInitData>()) {
}

Audio::MusicSequenceTable* MusicSequencesResource::GetPointer() {
    return &Data;
}

size_t MusicSequencesResource::GetPointerSize() {
    return Data.data.size();
}

} // namespace Frigate
