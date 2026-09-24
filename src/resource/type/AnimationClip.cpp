#include "resource/type/AnimationClip.h"

namespace Frigate {

AnimationClipResource::AnimationClipResource() : Resource(std::shared_ptr<Ship::ResourceInitData>()) {
}

uint8_t* AnimationClipResource::GetPointer() {
    return Bitstream.data();
}

size_t AnimationClipResource::GetPointerSize() {
    return Bitstream.size();
}

} // namespace Frigate
