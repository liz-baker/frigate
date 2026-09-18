#include "resource/type/Font.h"

namespace Frigate {

Font::Font() : Resource(std::shared_ptr<Ship::ResourceInitData>()) {
}

FontChar* Font::GetPointer() {
    return Chars.data();
}

size_t Font::GetPointerSize() {
    return Chars.size() * sizeof(FontChar);
}

} // namespace Frigate
