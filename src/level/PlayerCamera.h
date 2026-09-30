#pragma once

#include "gfx/FixedMtx.h"
#include "gfx/Gu.h"
#include "level/BgRender.h"
#include "level/BgView.h"
#include "level/BgVisibility.h"
#include "level/Frustum.h"
#include "level/Matrix.h"
#include "libultraship/libultra/gbi.h"

namespace Frigate::Level {

// What the level code reads off the current player each frame.
struct PlayerPose {
    // bondviewGetCurrentPlayersPosition().
    Vec3 position{};
    // bondviewGetCurrentPlayersPosition3(): where the player was last frame.
    Vec3 previousPosition{};
    // The camera's look direction and up vector.
    Vec3 look = { 0.0f, 0.0f, -1.0f };
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    // bondviewGetCurrentPlayersRoom(): the room the player's tile is in.
    int room = 1;
    CameraMode cameraMode = CameraMode::FirstPerson;
};

// The projection the current player draws with and the matrices built from
// it (the camera parts of `struct player`).
class PlayerCamera {
  public:
    PlayerCamera();

    // viSetViewSize, viSetViewPosition, and the player's screen fields the
    // camera scale is worked out from.
    void SetView(const ViewMetrics& vi);
    // viSetFovY, viSetAspect and the z range.
    void SetPerspective(float fovY, float aspect, float zNear, float zFar);
    // viSetZRange.
    void SetZRange(float zNear, float zFar) {
        SetPerspective(fovY_, aspect_, zNear, zFar);
    }
    // viSetFovY and viSetAspect, leaving the z range.
    void SetFovAspect(float fovY, float aspect) {
        SetPerspective(fovY, aspect, zNear_, zFar_);
    }

    // currentPlayerSetCameraScale.
    void UpdateScale();

    // viSetupCurrentPlayerView's viewport and projection, appended to `dl`.
    Gfx* SetupView(Gfx* dl);

    // bondviewUpdateCameraMatrices: the matrices the frame draws under, for
    // a camera in `pose.room` of `level`.
    void Update(const PlayerPose& pose, const BgLevel& level);

    // The visibility pass's view of the camera.
    VisibilityView Visibility(const PlayerPose& pose) const;
    // What bgLevelRender reads. The pointers stay valid until the next
    // Update.
    RenderView Render(const PlayerPose& pose, bool drawEnabled) const;

    const ViewMetrics& Metrics() const {
        return vi_;
    }
    // current_model_pos.
    const Vec3& ModelPosition() const {
        return modelPos_;
    }
    // current_room_pos.
    const Vec3& RoomPosition() const {
        return roomPos_;
    }
    // previous_model_pos.
    const Vec3& PreviousModelPosition() const {
        return previousModelPos_;
    }

    // c_screenleft, c_screentop, c_screenwidth and c_screenheight.
    float ScreenLeft() const {
        return static_cast<float>(vi_.viewLeft);
    }
    float ScreenTop() const {
        return static_cast<float>(vi_.viewTop);
    }
    float ScreenWidth() const {
        return static_cast<float>(vi_.viewWidth);
    }
    float ScreenHeight() const {
        return static_cast<float>(vi_.viewHeight);
    }

    // c_scalex, c_scaley and the terms derived from them.
    float ScaleX() const {
        return scaleX_;
    }
    float ScaleY() const {
        return scaleY_;
    }
    float RecipScaleX() const {
        return recipScaleX_;
    }
    float RecipScaleY() const {
        return recipScaleY_;
    }
    float LodScaleZ() const {
        return lodScaleZ_;
    }
    uint32_t LodScaleZU32() const {
        return lodScaleZU32_;
    }
    const Vec3& TopNormal() const {
        return topNormal_;
    }
    const Vec3& LeftNormal() const {
        return leftNormal_;
    }
    // The view matrix props draw under (see modelviewF_).
    const Gu::MatrixF& ModelviewF() const {
        return modelviewF_;
    }
    // bondviewUpdateFrustumPlanes's frustum, for a per-object screen-space
    // test (posIsOnScreen/camIsPosInScreen).
    const FrustumPlanes& Frustum() const {
        return frustum_;
    }
    // The world position the frustum was built from (bondviewGetCurrentPlayersPosition).
    const Vec3& WorldPosition() const {
        return worldPosition_;
    }
    // camGetWorldToScreenMtxf: the view matrix in world coordinates.
    const Gu::MatrixF& WorldToScreen() const {
        return worldToScreen_;
    }
    // currentPlayerGetProjectionMatrix: the perspective the frame draws with.
    const Mtx::FixedMtx& Projection() const {
        return projection_;
    }
    // What camIsPosInScreenBox reads of the camera.
    BoxCamera Box() const;
    // currentPlayerGetProjectionMatrixF: the same, in float form.
    const Gu::MatrixF& ProjectionF() const {
        return projectionF_;
    }
    // currentPlayerGetViewToWorldMtxf: rows 0 to 2 are the camera's right, up
    // and backward axes, row 3 its position.
    const Mtxf& ViewToWorld() const {
        return viewToWorld_;
    }

  private:
    ViewMetrics vi_;
    PlayerView player_;
    float fovY_ = 60.0f;
    float aspect_ = 4.0f / 3.0f;
    float zNear_ = 15.0f;
    float zFar_ = 10000.0f;

    float halfWidth_ = 0;
    float halfHeight_ = 0;
    float scaleX_ = 0;
    float scaleY_ = 0;
    float recipScaleX_ = 0;
    float recipScaleY_ = 0;
    float scaleLod_ = 0;
    float scaleLod60_ = 0;
    float lodScaleZ_ = 0;
    uint32_t lodScaleZU32_ = 0;
    Vec3 topNormal_{};
    Vec3 leftNormal_{};
    FrustumPlanes frustum_{};
    Vec3 worldPosition_{};
    Mtxf viewToWorld_{};

    Vec3 modelPos_{};
    Vec3 roomPos_{};
    Vec3 previousModelPos_{};

    Vp_t viewport_{};
    Mtx::FixedMtx projection_;
    Mtx::FixedMtx worldProjection_;
    Mtx::FixedMtx modelview_;
    // The float form of modelview_, kept for prop drawing (chrpropsRenderPass),
    // which needs the view matrix itself rather than a GBI-ready fixed one.
    Gu::MatrixF modelviewF_{};
    LookAt lookAt_{};
    Gu::MatrixF worldToScreen_{};
    // g_viProjectionMatrixF: what SetupView builds and Update combines with
    // the view.
    Gu::MatrixF projectionF_{};
};

} // namespace Frigate::Level
