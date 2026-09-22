#include "resource/type/ModelSkeleton.h"

namespace Frigate {

ModelSkeleton::ModelSkeleton() : Resource(std::shared_ptr<Ship::ResourceInitData>()) {
}

SkeletonJoint* ModelSkeleton::GetPointer() {
    return Joints.data();
}

size_t ModelSkeleton::GetPointerSize() {
    return Joints.size() * sizeof(SkeletonJoint);
}

} // namespace Frigate
