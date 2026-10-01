#include "level/PlayerCamera.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "gfx/GbiF3dex.h"

namespace Frigate::Level {

namespace {

constexpr float kDegToHalfRad = 3.14159265f / 360.0f;
constexpr float kDegToRad = 3.14159265f / 180.0f;
// D_800364CC: the camera position's scale.
constexpr float kCameraPositionScale = 1.0f;
// The largest a combined matrix entry gets, so it stays in fixed point range.
constexpr float kMatrixLimit = 32000.0f;

// Row-vector product: `a` applied first, then `b`.
void Multiply(const Gu::MatrixF& a, const Gu::MatrixF& b, Gu::MatrixF& out) {
    Gu::MatrixF product;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            product[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    std::memcpy(out, product, sizeof(product));
}

} // namespace

PlayerCamera::PlayerCamera() {
    SetView(ViewMetrics{ 320, 240, 0, 0, 320, 240 });
}

void PlayerCamera::SetView(const ViewMetrics& vi) {
    vi_ = vi;
    player_ = { vi.viewLeft, vi.viewTop, vi.viewWidth, vi.viewHeight };
    halfWidth_ = vi.viewWidth * 0.5f;
    halfHeight_ = vi.viewHeight * 0.5f;
    UpdateScale();
}

void PlayerCamera::SetPerspective(float fovY, float aspect, float zNear, float zFar) {
    fovY_ = fovY;
    aspect_ = aspect;
    zNear_ = zNear;
    zFar_ = zFar;
    UpdateScale();
}

void PlayerCamera::UpdateScale() {
    scaleY_ = std::sin(fovY_ * kDegToHalfRad) / (std::cos(fovY_ * kDegToHalfRad) * halfHeight_);
    scaleX_ = (scaleY_ * aspect_ * halfHeight_) / halfWidth_;
    recipScaleX_ = 1.0f / scaleX_;
    recipScaleY_ = 1.0f / scaleY_;

    scaleLod_ = scaleY_;
    scaleLod60_ = std::sin(30.0f * kDegToRad) / (std::cos(30.0f * kDegToRad) * 120.0f);
    lodScaleZ_ = scaleLod_ / scaleLod60_;
    const float scaled = lodScaleZ_ * 65535.0f;
    lodScaleZU32_ = scaled > 4294967295.0f ? 0xFFFFFFFFu : static_cast<uint32_t>(scaled);

    float extent = halfHeight_ * scaleY_;
    float norm = 1.0f / std::sqrt(extent * extent + 1.0f);
    topNormal_ = { 0.0f, norm, extent * norm };

    extent = -halfWidth_ * scaleX_;
    norm = 1.0f / std::sqrt(extent * extent + 1.0f);
    leftNormal_ = { -norm, 0.0f, -extent * norm };
}

Vec3 PlayerCamera::ScreenToView(float x, float y, float length) const {
    const float viewY = (halfHeight_ - (y - ScreenTop())) * scaleY_;
    const float viewX = ((x - ScreenLeft()) - halfWidth_) * scaleX_;
    const float viewZ = -1.0f;
    const float norm = length / std::sqrt((viewX * viewX) + (viewY * viewY) + (viewZ * viewZ));
    return { viewX * norm, viewY * norm, -1.0f * norm };
}

Gfx* PlayerCamera::SetupView(Gfx* dl) {
    viewport_.vscale[0] = static_cast<int16_t>(vi_.viewWidth * 2);
    viewport_.vtrans[0] = static_cast<int16_t>(vi_.viewWidth * 2 + vi_.viewLeft * 4);
    viewport_.vscale[1] = static_cast<int16_t>(vi_.viewHeight * 2);
    viewport_.vtrans[1] = static_cast<int16_t>(vi_.viewHeight * 2 + vi_.viewTop * 4);
    viewport_.vscale[2] = 0x1ff;
    viewport_.vtrans[2] = 0x1ff;
    *dl++ = F3dex::Viewport(&viewport_);

    Gu::PerspectiveF(projectionF_, fovY_, aspect_, zNear_, zFar_, 1.0f);
    projection_ = Mtx::ToFixedMtx(projectionF_);
    *dl++ = F3dex::Matrix(&projection_, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
    return dl;
}

void PlayerCamera::Update(const PlayerPose& pose, const BgLevel& level) {
    // bondviewUpdateCurrentRoomPosition: getRoomPositionScaledByIndex and its
    // product with the stage scale.
    const float roomScale = 1.0f / level.Level().scale;
    const Vec3& origin = level.RoomEntry(pose.room).position;
    for (int i = 0; i < 3; i++) {
        modelPos_[i] = origin[i] * roomScale;
        roomPos_[i] = modelPos_[i] * level.Level().scale;
    }

    const Vec3 scaled = { (pose.position[0] - modelPos_[0]) * kCameraPositionScale,
                          (pose.position[1] - modelPos_[1]) * kCameraPositionScale,
                          (pose.position[2] - modelPos_[2]) * kCameraPositionScale };
    const Vec3 target = { scaled[0] + pose.look[0], scaled[1] + pose.look[1], scaled[2] + pose.look[2] };

    Gu::MatrixF view;
    Gu::LookAtF(view, scaled[0], scaled[1], scaled[2], target[0], target[1], target[2], pose.up[0], pose.up[1],
                pose.up[2]);
    Gu::LookAtReflect(lookAt_, scaled[0], scaled[1], scaled[2], target[0], target[1], target[2], pose.up[0], pose.up[1],
                      pose.up[2]);
    Gu::LookAtF(worldToScreen_, pose.position[0], pose.position[1], pose.position[2], pose.position[0] + pose.look[0],
                pose.position[1] + pose.look[1], pose.position[2] + pose.look[2], pose.up[0], pose.up[1], pose.up[2]);
    // currentPlayerSetViewToWorldMtxf/bondviewUpdateFrustumPlanes: the
    // camera-to-world basis and the frustum built from it, for the per-object
    // screen-space test (PropRender::UpdateOnScreenList).
    worldPosition_ = pose.position;
    const CameraAxes axes = ComputeCameraAxes(pose.look, pose.up);
    for (int i = 0; i < 3; i++) {
        viewToWorld_.m[0][i] = axes.right[i];
        viewToWorld_.m[1][i] = axes.up[i];
        viewToWorld_.m[2][i] = axes.basis[i];
        viewToWorld_.m[3][i] = pose.position[i];
        viewToWorld_.m[i][3] = i == 3 ? 1.0f : 0.0f;
    }
    viewToWorld_.m[3][3] = 1.0f;
    frustum_ = ComputeFrustumPlanes(pose.position, pose.look, pose.up, topNormal_, leftNormal_);

    Gu::MatrixF combined;
    Multiply(view, projectionF_, combined);
    for (auto& row : combined) {
        for (float& value : row) {
            value = std::clamp(value, -kMatrixLimit, kMatrixLimit);
        }
    }
    worldProjection_ = Mtx::ToFixedMtx(combined);

    // The view props draw under. The stage's visibility scale only keeps the
    // game's fixed-point matrices in range, and the rooms are not scaled by
    // it here, so neither is this: scaling only the props would put their
    // depths nearer than the walls in front of them.
    modelview_ = Mtx::ToFixedMtx(view);
    std::memcpy(modelviewF_, view, sizeof(modelviewF_));

    // store_BONDdata_curpos_to_previous: the model position rotated into view.
    for (int i = 0; i < 3; i++) {
        previousModelPos_[i] = worldToScreen_[0][i] * modelPos_[0] + worldToScreen_[1][i] * modelPos_[1] +
                               worldToScreen_[2][i] * modelPos_[2];
    }
}

BoxCamera PlayerCamera::Box() const {
    BoxCamera box;
    box.viewToWorld = viewToWorld_;
    box.nearOffset = frustum_.nearOffset;
    box.screenLeft = ScreenLeft();
    box.screenTop = ScreenTop();
    box.halfWidth = halfWidth_;
    box.halfHeight = halfHeight_;
    box.scaleX = scaleX_;
    box.scaleY = scaleY_;
    return box;
}

VisibilityView PlayerCamera::Visibility(const PlayerPose& pose) const {
    VisibilityView view;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            view.worldToScreen[i][j] = worldToScreen_[i][j];
        }
    }
    view.position = pose.position;
    view.screen = ScreenMinMax(vi_);
    view.zFar = zFar_;
    view.screenLeft = static_cast<float>(vi_.viewLeft);
    view.screenTop = static_cast<float>(vi_.viewTop);
    view.halfWidth = halfWidth_;
    view.halfHeight = halfHeight_;
    view.recipScaleX = recipScaleX_;
    view.recipScaleY = recipScaleY_;
    view.currentRoom = pose.room;
    return view;
}

RenderView PlayerCamera::Render(const PlayerPose& pose, bool drawEnabled) const {
    RenderView view;
    view.player = player_;
    view.currentRoom = pose.room;
    view.modelPos = modelPos_;
    view.propProjection = &projection_;
    view.worldProjection = &worldProjection_;
    view.viProjection = &projection_;
    view.modelview = &modelview_;
    view.modelviewF = &modelviewF_;
    view.lookAt = &lookAt_;
    view.drawEnabled = drawEnabled;
    view.lodScaleZ = lodScaleZ_;
    return view;
}

} // namespace Frigate::Level
