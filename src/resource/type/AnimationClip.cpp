#include "resource/type/AnimationClip.h"

namespace Frigate {

AnimationClip::AnimationClip() : Resource(std::shared_ptr<Ship::ResourceInitData>()) {
}

uint8_t* AnimationClip::GetPointer() {
    return Bitstream.data();
}

size_t AnimationClip::GetPointerSize() {
    return Bitstream.size();
}

} // namespace Frigate
