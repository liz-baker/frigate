#include "resource/factory/AnimationClipFactory.h"
#include "resource/type/AnimationClip.h"

namespace Frigate {

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryAnimationClipV0::ReadResource(std::shared_ptr<Ship::File> file,
                                                   std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);
    auto clip = std::make_shared<AnimationClip>(initData);

    clip->FrameCount = reader->ReadUInt32();
    clip->AngleWidthBits = reader->ReadUByte();
    clip->Loop = (reader->ReadUByte() & 1) != 0;
    reader->ReadUInt16();
    clip->FrameSizeBytes = reader->ReadUInt32();

    // Reject a frame count/size the file cannot hold before reserving.
    const uint64_t bitstreamSize = static_cast<uint64_t>(clip->FrameCount) * clip->FrameSizeBytes;
    if (bitstreamSize > file->Buffer->size() - reader->GetBaseAddress()) {
        return nullptr;
    }

    clip->Bitstream.resize(bitstreamSize);
    for (uint8_t& byte : clip->Bitstream) {
        byte = reader->ReadUByte();
    }
    return clip;
}

} // namespace Frigate
