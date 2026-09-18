#include "resource/type/Text.h"

namespace Frigate {

Text::Text() : Resource(std::shared_ptr<Ship::ResourceInitData>()) {
    Strings.resize(kOffsetTableSize);
}

std::string* Text::GetPointer() {
    return Strings.data();
}

size_t Text::GetPointerSize() {
    return Strings.size() * sizeof(std::string);
}

} // namespace Frigate
