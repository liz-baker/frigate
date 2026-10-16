#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Frigate::Extract {

// What the GoldenEye decomp keeps in code for each chr/gun/prop model file: its MODELFILEHEADER row. The file has
// no header of its own (docs/formats/goldeneye-containers.md).
struct ModelHeaderEntry {
    const char* name; // the header name; the file is that with a one-letter directory prefix and a `Z` suffix
    int32_t numSwitches;
    int32_t numTextures;
    const char* skeleton; // empty when the model has none
    float radius;         // BoundingVolumeRadius
};

struct SkeletonJoint {
    uint16_t nodeType;
    uint16_t matrixA;
    uint16_t matrixB;
};

struct SkeletonEntry {
    const char* name;
    uint32_t firstJoint;
    uint32_t jointCount;
};

// nullptr when the model is not in the table.
const ModelHeaderEntry* FindModelHeader(std::string_view name);
// The joints of a skeleton; empty when it is not in the table.
std::span<const SkeletonJoint> FindSkeleton(std::string_view name, bool* found = nullptr);

namespace Detail {
// Defined in the generated ModelTablesData.cpp.
extern const ModelHeaderEntry kModelHeaders[];
extern const size_t kModelHeaderCount;
extern const SkeletonJoint kSkeletonJoints[];
extern const SkeletonEntry kSkeletons[];
extern const size_t kSkeletonCount;
} // namespace Detail

} // namespace Frigate::Extract
