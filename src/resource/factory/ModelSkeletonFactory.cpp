#include "resource/factory/ModelSkeletonFactory.h"
#include "resource/type/ModelSkeleton.h"

namespace Frigate {

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryModelSkeletonV0::ReadResource(std::shared_ptr<Ship::File> file,
                                                   std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);
    auto skeleton = std::make_shared<ModelSkeleton>(initData);

    const uint32_t count = reader->ReadUInt32();
    // Each joint record is 6 bytes; reject a count the file cannot hold.
    constexpr size_t kJointSize = 6;
    if (count > file->Buffer->size() / kJointSize) {
        return nullptr;
    }

    skeleton->Joints.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        SkeletonJoint joint;
        joint.nodeType = reader->ReadUInt16();
        joint.mtxA = reader->ReadUInt16();
        joint.mtxB = reader->ReadUInt16();
        skeleton->Joints.push_back(joint);
    }
    return skeleton;
}

} // namespace Frigate
