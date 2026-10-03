#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frigate::Game::Menu::FolderScreenSpace {

using Point3 = std::array<float, 3>;

struct Point2 {
    float x;
    float y;
};

constexpr int32_t kFolderCount = 4;

// The camera looks at the origin from this far along +Z (the file select's
// matrix_4x4_set_lookat_target).
constexpr float kCameraDistance = 4000.0f;
// The wallet models' scale (matrix_scalar_multiply(0.37f, ...)).
constexpr float kFolderScale = 0.37f;

// folderpositions: where each wallet sits in the world.
constexpr std::array<Point3, kFolderCount> kFolderPositions = { {
    { -900.0f, 800.0f, 0.0f },
    { 1800.0f, 800.0f, 0.0f },
    { -1800.0f, -200.0f, 0.0f },
    { 900.0f, -200.0f, 0.0f },
} };

// folderpositions_camspace: the same in view space.
Point3 CameraSpacePosition(int32_t folder);

// transform3Dto2DCoords under the front-end's view: the 440x330 screen at
// (0, 0), a 60 degree field of view and a 4:3 aspect.
Point2 ProjectToScreen(const Point3& position);

} // namespace Frigate::Game::Menu::FolderScreenSpace
