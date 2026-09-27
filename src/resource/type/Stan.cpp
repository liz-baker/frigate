#include "resource/type/Stan.h"

namespace Frigate {

StanResource::StanResource() : Resource(std::shared_ptr<Ship::ResourceInitData>()) {
}

Level::StanData* StanResource::GetPointer() {
    return &Data;
}

size_t StanResource::GetPointerSize() {
    return Data.tiles.size() * sizeof(Level::StanTile);
}

} // namespace Frigate
