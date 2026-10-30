#pragma once

#include "extract/DecompTables.h"

#include <span>
#include <string_view>

namespace Frigate::Extract {

// nullptr when the model is not in kModelHeaders. The file has no header of its own
// (docs/formats/goldeneye-containers.md); the decomp keeps its MODELFILEHEADER row in code.
const ModelHeader* FindModelHeader(std::string_view name);
// The joints of a skeleton; empty when it is not in the table.
std::span<const SkeletonJoint> FindSkeleton(std::string_view name, bool* found = nullptr);

} // namespace Frigate::Extract
