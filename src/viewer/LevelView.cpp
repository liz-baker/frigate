#include "viewer/LevelView.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <optional>

#include <spdlog/spdlog.h>

#include "fast/Fast3dWindow.h"
#include "gfx/GbiF3dex.h"
#include "level/ResourceBgSource.h"
#include "level/Stage.h"
#include "ship/core/Context.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Viewer {

namespace {

constexpr int32_t kWidth = 320;
constexpr int32_t kHeight = 240;

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

// The room whose bounds are closest to `position` (inside counts as 0), the
// smaller one on a tie.
// TODO(https://github.com/liz-baker/frigate/issues/35): the game takes the room
// from the player's stand tile.
int NearestRoom(const Level::BgLevel& level, const std::array<float, 3>& position) {
    const float scale = level.Level().scale;
    int best = 1;
    float bestDistance = std::numeric_limits<float>::max();
    float bestVolume = std::numeric_limits<float>::max();
    for (int room = 1; room < level.MaxNumRooms(); room++) {
        const Level::RoomInfo& info = level.Room(room);
        float distanceSquared = 0;
        float volume = 1;
        for (int axis = 0; axis < 3; axis++) {
            const float p = position[axis] * scale;
            const float outside = std::max({ info.minBounds[axis] - p, p - info.maxBounds[axis], 0.0f });
            distanceSquared += outside * outside;
            volume *= info.maxBounds[axis] - info.minBounds[axis];
        }
        if (distanceSquared < bestDistance || (distanceSquared == bestDistance && volume < bestVolume)) {
            best = room;
            bestDistance = distanceSquared;
            bestVolume = volume;
        }
    }
    return best;
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

    const auto id = FindStage(options.stage);
    if (!id) {
        spdlog::error("level view: no stage with bg directory '{}'", options.stage);
        return 1;
    }
    Level::ResourceBgSource source(resourceManager);
    Level::Stage stage(source);
    if (!stage.Load(*id, options.demoSlot)) {
        spdlog::error("level view: {}", stage.Error());
        return 1;
    }
    const Level::BgLevel& level = stage.Bg();
    if (options.room && (*options.room < 1 || *options.room >= level.MaxNumRooms())) {
        spdlog::error("level view: room {} is not in 1..{}", *options.room, level.MaxNumRooms() - 1);
        return 1;
    }

    // Stands in for the player: a fixed camera at the stage's spawn. The game
    // moves the player by the demo's recorded input.
    const bool hasSpawn = !stage.Intro().startPads.empty();
    Level::PlayerPose pose;
    pose.look = options.hasLook ? options.look
                : hasSpawn      ? stage.Spawn().look
                                : std::array<float, 3>{ 0.0f, 0.0f, -1.0f };
    // Every visible room loads on the first frame.
    pose.cameraMode = Level::CameraMode::Intro;
    if (options.hasPosition) {
        pose.position = options.position;
    } else if (hasSpawn && !options.room) {
        pose.position = stage.Spawn().EyePosition();
    } else {
        const int room = options.room.value_or(1);
        const float roomScale = 1.0f / level.Level().scale;
        const Level::RoomInfo& info = level.Room(room);
        for (int axis = 0; axis < 3; axis++) {
            pose.position[axis] = (info.minBounds[axis] + info.maxBounds[axis]) * 0.5f * roomScale;
        }
    }
    pose.room = options.room ? *options.room : NearestRoom(level, pose.position);
    pose.previousPosition = pose.position;
    spdlog::info("level view: {} room {} camera at ({}, {}, {})", options.stage, pose.room, pose.position[0],
                 pose.position[1], pose.position[2]);

    while (window->IsRunning()) {
        window->HandleEvents();
        stage.Tick();

        constexpr size_t kFrameCapacity = 4096;
        Gfx frame[kFrameCapacity];
        Gfx* dl = ClearFrame(frame);
        dl = stage.Render(dl, pose);
        *dl++ = F3dex::EndDisplayList();
        assert(static_cast<size_t>(dl - frame) <= kFrameCapacity);

        window->DrawAndRunGraphicsCommands(frame, {});
    }

    stage.Unload();
    window->Close();
    return 0;
}

} // namespace Frigate::Viewer
