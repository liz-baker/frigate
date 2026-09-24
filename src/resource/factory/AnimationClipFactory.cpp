#include "resource/factory/AnimationClipFactory.h"
#include "resource/type/AnimationClip.h"

namespace Frigate {

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryAnimationClipV1::ReadResource(std::shared_ptr<Ship::File> file,
                                                   std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);
    auto clip = std::make_shared<AnimationClipResource>(initData);

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

    // Root-motion block: stride, descriptor count and bitstream length, then
    // 5-byte descriptors, then the bitstream.
    constexpr uint64_t kDescriptorSize = 5;
    clip->RootStrideBits = reader->ReadUInt16();
    const uint16_t descriptorCount = reader->ReadUInt16();
    const uint32_t rootBitstreamSize = reader->ReadUInt32();

    const uint64_t rootSize = descriptorCount * kDescriptorSize + static_cast<uint64_t>(rootBitstreamSize);
    if (rootSize > file->Buffer->size() - reader->GetBaseAddress()) {
        return nullptr;
    }

    clip->RootMotionDescriptors.resize(descriptorCount);
    for (RootMotionDescriptor& descriptor : clip->RootMotionDescriptors) {
        descriptor.bitOffset = reader->ReadUInt16();
        descriptor.bitCount = reader->ReadUByte();
        descriptor.valueOffset = reader->ReadUInt16();
    }
    clip->RootMotionBitstream.resize(rootBitstreamSize);
    for (uint8_t& byte : clip->RootMotionBitstream) {
        byte = reader->ReadUByte();
    }

    clip->View.angleWidthBits = clip->AngleWidthBits;
    clip->View.frameSizeBytes = clip->FrameSizeBytes;
    clip->View.bitstream = clip->Bitstream.data();
    clip->View.frameCount = clip->FrameCount;
    clip->View.loop = clip->Loop;
    clip->View.rootStrideBits = clip->RootStrideBits;
    clip->View.rootDescriptors = clip->RootMotionDescriptors.data();
    clip->View.rootDescriptorCount = static_cast<uint32_t>(clip->RootMotionDescriptors.size());
    clip->View.rootBitstream = clip->RootMotionBitstream.data();
    return clip;
}

} // namespace Frigate
