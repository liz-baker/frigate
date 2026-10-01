#pragma once

#include <memory>
#include <string>

#include "level/Doors.h"
#include "level/PropModels.h"
#include "model/ModelDraw.h"

namespace Ship {
class ResourceManager;
} // namespace Ship

namespace Frigate::Level {

// What a door draws differently from its model as the file has it: the leaves
// of an eyelid or iris door turn about their hinges as it opens (the matrices
// sub_GAME_7F04AC20 builds for its skeleton), and a door that clips to its
// box has its mesh resized (doorBuildClippedVertices). `base` is the door's
// world transform under the view; `modelPath` is the model's resource path.
// Leaves `overrides` as it was for any other door.
void FillDoorOverrides(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const std::string& modelPath,
                       const PropModel& model, const PlacedDoor& door, const Mat4& base,
                       Model::ModelDrawOverrides* overrides);

} // namespace Frigate::Level
