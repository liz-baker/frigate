#include "extract/ModelTables.h"

#include <algorithm>

namespace Frigate::Extract {

const ModelHeaderEntry* FindModelHeader(std::string_view name) {
    const ModelHeaderEntry* begin = Detail::kModelHeaders;
    const ModelHeaderEntry* end = begin + Detail::kModelHeaderCount;
    const ModelHeaderEntry* it =
        std::lower_bound(begin, end, name, [](const ModelHeaderEntry& e, std::string_view n) { return e.name < n; });
    return (it != end && it->name == name) ? it : nullptr;
}

std::span<const SkeletonJoint> FindSkeleton(std::string_view name, bool* found) {
    const SkeletonEntry* begin = Detail::kSkeletons;
    const SkeletonEntry* end = begin + Detail::kSkeletonCount;
    const SkeletonEntry* it =
        std::lower_bound(begin, end, name, [](const SkeletonEntry& e, std::string_view n) { return e.name < n; });
    bool match = it != end && it->name == name;
    if (found != nullptr) {
        *found = match;
    }
    if (!match) {
        return {};
    }
    return std::span<const SkeletonJoint>(Detail::kSkeletonJoints + it->firstJoint, it->jointCount);
}

} // namespace Frigate::Extract
