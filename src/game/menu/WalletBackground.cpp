#include "game/menu/WalletBackground.h"

#include "game/menu/BasicGeometry.h"
#include "game/menu/FolderScreenSpace.h"
#include "game/menu/PropCamera.h"
#include "game/menu/WalletBond.h"
#include "gfx/GbiF3dex.h"
#include "gfx/Gu.h"
#include "model/ModelInstance.h"

namespace Frigate::Game::Menu {

namespace {

namespace Space = FolderScreenSpace;

// viSetFovY(FOV_Y_F), viSetAspect(ASPECT_RATIO_SD) and viSetZRange(100, 10000).
constexpr float kFovY = 60.0f;
constexpr float kAspect = 1.3333334f;
constexpr float kNear = 100.0f;
constexpr float kFar = 10000.0f;

// The wallet's scale, and the offsets the camera's target takes from the
// folder's place (D_8002AFC4, D_8002AFC8 and D_8002AFCC, the last also taken
// off the camera's 4000 units of distance).
constexpr float kWalletScale = 0.25f;
constexpr float kTargetOffsetX = 0.0f;
constexpr float kTargetOffsetY = 190.0f;
constexpr float kCameraDistanceOffset = -3300.0f;

// The folder's place and scale, then the lookat at the target above it: eye
// (x, y, 4000 - 3300) looking down -Z, a translation of minus the eye.
Mat4 WalletMatrix(int32_t folder) {
    const Space::Point3& place = Space::kFolderPositions[static_cast<size_t>(folder)];
    const float targetX = place[0] + kTargetOffsetX;
    const float targetY = place[1] + kTargetOffsetY;
    const float eyeZ = Space::kCameraDistance + kCameraDistanceOffset;
    Mat4 matrix = IdentityMat4();
    matrix.m[0][0] = kWalletScale;
    matrix.m[1][1] = kWalletScale;
    matrix.m[2][2] = kWalletScale;
    matrix.m[3][0] = place[0] - targetX;
    matrix.m[3][1] = place[1] - targetY;
    matrix.m[3][2] = place[2] - eyeZ;
    return matrix;
}

} // namespace

WalletBackground::WalletBackground(std::shared_ptr<Ship::ResourceManager> resourceManager)
    : resourceManager_(std::move(resourceManager)) {
}

void WalletBackground::Init() {
    viewport_ = MenuViewport();
    Gu::MatrixF matrix;
    Gu::TranslateF(matrix, 0.0f, 0.0f, 0.0f);
    view_ = Mtx::ToFixedMtx(matrix);
    Gu::PerspectiveF(matrix, kFovY, kAspect, kNear, kFar, 1.0f);
    projection_ = Mtx::ToFixedMtx(matrix);
}

void WalletBackground::Release() {
    draw_.reset();
}

Gfx* WalletBackground::Draw(Gfx* dl, WalletBond& wallets, int32_t folder) {
    using namespace Frigate::F3dex;
    draw_.reset();
    if (!wallets.Loaded()) {
        return dl;
    }
    // The wallet's one matrix is set directly, as the decomp copies it into the
    // model's matrix list; WalletBond::Load has cleared the root origin the
    // matrix calculation would otherwise add.
    wallets.Instance(0)->Pose(WalletMatrix(folder));

    dl = BasicGeometry(dl);
    dl = FastPipelineSetup(dl);
    // The modelview loads first: the interpreter multiplies a projection load
    // by the top of the modelview stack, which is empty until the first
    // modelview load.
    *dl++ = Viewport(&viewport_);
    *dl++ = Matrix(&view_, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
    *dl++ = Matrix(&projection_, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);

    // sp10C.flags = 3, no z-buffer: the primary and secondary lists node by node.
    ModelRenderParams params;
    params.zbufferEnabled = false;
    params.passes = 3;
    params.sortJoints = false;
    draw_ = Model::BuildJointDraw(resourceManager_, { wallets.Instance(0) }, params);
    if (draw_) {
        for (const Gfx& command : draw_->commands) {
            *dl++ = command;
        }
    }
    return dl;
}

} // namespace Frigate::Game::Menu
