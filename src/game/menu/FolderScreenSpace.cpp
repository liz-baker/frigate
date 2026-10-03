#include "game/menu/FolderScreenSpace.h"

#include "model/GameMath.h"

namespace Frigate::Game::Menu::FolderScreenSpace {

namespace {

constexpr float kScreenLeft = 0.0f;
constexpr float kScreenTop = 0.0f;
constexpr float kHalfWidth = 440.0f * 0.5f;
constexpr float kHalfHeight = 330.0f * 0.5f;
constexpr float kFovY = 60.0f;
constexpr float kAspect = 1.3333334f; // ASPECT_RATIO_SD

struct RecipScale {
    float x;
    float y;
};

// currentPlayerSetCameraScale's c_recipscalex and c_recipscaley.
RecipScale ComputeRecipScale() {
    constexpr float kHalfRadians = (kFovY * 3.1415927f) / 360.0f; // mDegToHalfRad
    const float scaleY = GameSinf(kHalfRadians) / (GameCosf(kHalfRadians) * kHalfHeight);
    const float scaleX = (scaleY * kAspect * kHalfHeight) / kHalfWidth;
    return { 1.0f / scaleX, 1.0f / scaleY };
}

} // namespace

Point3 CameraSpacePosition(int32_t folder) {
    // The wallet's matrix is its position and scale, multiplied by the lookat
    // (a translation of -kCameraDistance along Z).
    const Point3& position = kFolderPositions[static_cast<size_t>(folder)];
    return { position[0], position[1], position[2] - kCameraDistance };
}

Point2 ProjectToScreen(const Point3& position) {
    static const RecipScale kRecip = ComputeRecipScale();
    const float invZ = 1.0f / position[2];
    return { (kScreenLeft + kHalfWidth) - (position[0] * invZ * kRecip.x),
             (position[1] * invZ * kRecip.y) + (kScreenTop + kHalfHeight) };
}

} // namespace Frigate::Game::Menu::FolderScreenSpace
