#include "viewer/LevelView.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

#include <spdlog/spdlog.h>

#include "fast/Fast3dWindow.h"
#include "game/menu/BasicGeometry.h"
#include "gfx/GbiF3dex.h"
#include "gfx/Gu.h"
#include "level/BgRender.h"
#include "level/ResourceBgSource.h"
#include "ship/core/Context.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Viewer {

namespace {

using Level::Vec3;

constexpr int32_t kWidth = 320;
constexpr int32_t kHeight = 240;
// FOV_Y_F and viSetZRange(15, 10000).
constexpr float kFovY = 60.0f;
constexpr float kNear = 15.0f;
constexpr float kFar = 10000.0f;
constexpr float kAspect = static_cast<float>(kWidth) / kHeight;
// The load budget bgRoomVisibilityRelated grants the cinematic camera modes,
// so every visible room is there on the first frame.
constexpr int kRoomLoadBudget = 200;

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

// The camera's matrices, as bondviewUpdateCameraMatrices() builds them.
struct Camera {
    Mtx::FixedMtx projection;
    Mtx::FixedMtx worldProjection;
    Mtx::FixedMtx modelview;
    LookAt lookAt{};
    Gu::MatrixF worldToScreen;
    Vp_t viewport{};
};

Camera BuildCamera(const Vec3& position, const Vec3& look, const Vec3& modelPos, float visibilityScale) {
    Camera camera;
    const Vec3 scaled = { position[0] - modelPos[0], position[1] - modelPos[1], position[2] - modelPos[2] };

    Gu::MatrixF view;
    Gu::LookAtF(view, scaled[0], scaled[1], scaled[2], scaled[0] + look[0], scaled[1] + look[1], scaled[2] + look[2],
                0.0f, 1.0f, 0.0f);
    Gu::LookAtReflect(camera.lookAt, scaled[0], scaled[1], scaled[2], scaled[0] + look[0], scaled[1] + look[1],
                      scaled[2] + look[2], 0.0f, 1.0f, 0.0f);
    Gu::LookAtF(camera.worldToScreen, position[0], position[1], position[2], position[0] + look[0],
                position[1] + look[1], position[2] + look[2], 0.0f, 1.0f, 0.0f);

    Gu::MatrixF projection;
    Gu::PerspectiveF(projection, kFovY, kAspect, kNear, kFar, 1.0f);
    camera.projection = Mtx::ToFixedMtx(projection);

    Gu::MatrixF combined;
    Multiply(view, projection, combined);
    for (auto& row : combined) {
        for (float& value : row) {
            value = std::fmin(std::fmax(value, -32000.0f), 32000.0f);
        }
    }
    camera.worldProjection = Mtx::ToFixedMtx(combined);

    // The view matrix props draw under: the 3x3 block scaled by the stage's
    // visibility scale.
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            view[i][j] *= visibilityScale;
        }
    }
    camera.modelview = Mtx::ToFixedMtx(view);

    camera.viewport.vscale[0] = (kWidth / 2) * 4;
    camera.viewport.vscale[1] = (kHeight / 2) * 4;
    camera.viewport.vscale[2] = 0x1ff;
    camera.viewport.vtrans[0] = (kWidth / 2) * 4;
    camera.viewport.vtrans[1] = (kHeight / 2) * 4;
    camera.viewport.vtrans[2] = 0x1ff;
    return camera;
}

// The visibility pass's view of the camera: currentPlayerSetCameraScale().
Level::VisibilityView BuildVisibilityView(const Camera& camera, const Vec3& position, int room,
                                          const Level::ViewMetrics& vi) {
    Level::VisibilityView view;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            view.worldToScreen[i][j] = camera.worldToScreen[i][j];
        }
    }
    view.position = position;
    view.screen = Level::ScreenMinMax(vi);
    view.zFar = kFar;
    view.screenLeft = static_cast<float>(vi.viewLeft);
    view.screenTop = static_cast<float>(vi.viewTop);
    view.halfWidth = vi.viewWidth * 0.5f;
    view.halfHeight = vi.viewHeight * 0.5f;
    constexpr float kDegToHalfRad = 3.14159265f / 360.0f;
    const float scaleY = std::sin(kFovY * kDegToHalfRad) / (std::cos(kFovY * kDegToHalfRad) * view.halfHeight);
    const float scaleX = scaleY * kAspect * view.halfHeight / view.halfWidth;
    view.recipScaleX = 1.0f / scaleX;
    view.recipScaleY = 1.0f / scaleY;
    view.currentRoom = room;
    return view;
}

Gfx* ClearFrame(Gfx* dl) {
    using namespace Frigate::F3dex;
    gDPSetColorImage(dl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, kWidth, reinterpret_cast<void*>(uintptr_t{ 0x00100000 }));
    gDPSetScissor(dl++, G_SC_NON_INTERLACE, 0, 0, kWidth, kHeight);
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_FILL);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_NOOP | G_RM_NOOP2);
    // Magenta, so any hole in the geometry shows.
    gDPSetFillColor(dl++, (GPACK_RGBA5551(255, 0, 255, 1) << 16) | GPACK_RGBA5551(255, 0, 255, 1));
    gDPFillRectangle(dl++, 0, 0, kWidth - 1, kHeight - 1);
    gDPPipeSync(dl++);
    return dl;
}

std::optional<Level::LevelId> FindStage(const std::string& bgName) {
    for (size_t i = 0;; i++) {
        const Level::LevelEntry& entry = Level::LevelEntryAt(i);
        if (bgName == entry.bgName) {
            return entry.id;
        }
        if (entry.id == Level::LevelId::Max) {
            return std::nullopt;
        }
    }
}

} // namespace

int RunLevel(const std::shared_ptr<Ship::Context>& context, const LevelOptions& options) {
    auto window = context->GetChildren().GetFirst<Fast::Fast3dWindow>();
    auto resourceManager = context->GetChildren().GetFirst<Ship::ResourceManager>();

    const auto stage = FindStage(options.stage);
    if (!stage) {
        spdlog::error("level view: no stage with bg directory '{}'", options.stage);
        return 1;
    }
    Level::ResourceBgSource source(resourceManager);
    Level::BgLevel level(source);
    if (!level.Load(*stage)) {
        spdlog::error("level view: {}", level.Error());
        return 1;
    }
    if (options.room < 1 || options.room >= level.MaxNumRooms()) {
        spdlog::error("level view: room {} is not in 1..{}", options.room, level.MaxNumRooms() - 1);
        return 1;
    }
    Level::BgVisibility visibility(level);
    Level::BgRender render(level, visibility);
    render.Reset();

    // getRoomPositionScaledByIndex: the current room's origin, which every
    // room's matrix is relative to.
    const float roomScale = 1.0f / level.Level().scale;
    const Level::BgRoomEntry& entry = level.RoomEntry(options.room);
    const Vec3 modelPos = { entry.position[0] * roomScale, entry.position[1] * roomScale,
                            entry.position[2] * roomScale };

    Vec3 position{};
    if (options.hasPosition) {
        position = options.position;
    } else {
        const Level::RoomInfo& info = level.Room(options.room);
        for (int axis = 0; axis < 3; axis++) {
            position[axis] = (info.minBounds[axis] + info.maxBounds[axis]) * 0.5f * roomScale;
        }
    }
    spdlog::info("level view: {} room {} camera at ({}, {}, {})", options.stage, options.room, position[0], position[1],
                 position[2]);

    const Level::ViewMetrics vi{ kWidth, kHeight, 0, 0, kWidth, kHeight };
    Camera camera = BuildCamera(position, options.look, modelPos, level.VisibilityScale());

    Level::RenderView view;
    view.player = { 0, 0, kWidth, kHeight };
    view.currentRoom = options.room;
    view.modelPos = modelPos;
    view.propProjection = &camera.projection;
    view.worldProjection = &camera.worldProjection;
    view.viProjection = &camera.projection;
    view.modelview = &camera.modelview;
    view.lookAt = &camera.lookAt;

    while (window->IsRunning()) {
        window->HandleEvents();

        // The parts of bgTick and lvTick the stage load will own.
        visibility.ClearCommandRooms();
        level.TickUnload();
        level.SetRoomLoadBudget(kRoomLoadBudget);
        render.Tick();
        visibility.DetermineVisibleRooms(BuildVisibilityView(camera, position, options.room, vi));

        constexpr size_t kFrameCapacity = 4096;
        Gfx frame[kFrameCapacity];
        Gfx* dl = ClearFrame(frame);
        dl = Game::Menu::FastPipelineSetup(dl);
        dl = Game::Menu::ZBufferGeometry(dl);
        *dl++ = F3dex::Viewport(&camera.viewport);
        dl = render.Render(dl, view);
        *dl++ = F3dex::EndDisplayList();
        assert(static_cast<size_t>(dl - frame) <= kFrameCapacity);

        window->DrawAndRunGraphicsCommands(frame, {});
    }

    window->Close();
    return 0;
}

} // namespace Frigate::Viewer
