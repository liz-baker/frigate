#include "viewer/LevelView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cassert>
#include <optional>

#include <spdlog/spdlog.h>

#include "fast/Fast3dWindow.h"
#include "game/DemoRun.h"
#include "gfx/DisplayAspect.h"
#include "game/FrameTiming.h"
#include "game/save/SaveData.h"
#include "gfx/GbiF3dex.h"
#include "level/PlayerPadInput.h"
#include "level/ResourceBgSource.h"
#include "level/Stage.h"
#include "model/Random.h"
#include "platform/Input.h"
#include "ship/controller/controldeck/ControlDeck.h"
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
    gDPSetFillColor(dl++, (GPACK_RGBA5551(0, 0, 0, 1) << 16) | GPACK_RGBA5551(0, 0, 0, 1));
    gDPFillRectangle(dl++, 0, 0, kWidth - 1, kHeight - 1);
    gDPPipeSync(dl++);
    return dl;
}

// The room of the stan tile nearest `position`, which is how the game places a
// player without a start pad; room 1 when the stage has no tiles.
int RoomAt(Level::Stan& stan, std::array<float, 3> position) {
    const int tile = stan.FindNearestWalkable(&position, 0.0f);
    return tile == Level::kNoTile ? 1 : stan.TileRoom(tile);
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

// Plays one demo from its stage load to the fade to the title, then closes.
int RunDemo(const std::shared_ptr<Fast::Fast3dWindow>& window,
            const std::shared_ptr<Ship::ResourceManager>& resourceManager, const LevelOptions& options) {
    Game::DemoRunOptions demoOptions;
    demoOptions.fadeOnRandomMismatch = options.fadeOnRandomMismatch;
    demoOptions.realtime = !options.uncapped;
    if (*options.demo < 0) {
        spdlog::error("level view: no demo {}", *options.demo);
        return 1;
    }
    Display::SetLivePlay(false);
    auto run = Game::DemoRun::Start(resourceManager, static_cast<size_t>(*options.demo), demoOptions);
    if (!run) {
        return 1;
    }
    while (window->IsRunning() && !run->Done()) {
        window->HandleEvents();
        Display::SyncDisplaySize(*window);
        run->Step(/*buttonPressed=*/false);

        constexpr size_t kFrameCapacity = 16384;
        Gfx frame[kFrameCapacity];
        Gfx* dl = run->Render(frame);
        *dl++ = F3dex::EndDisplayList();
        assert(static_cast<size_t>(dl - frame) <= kFrameCapacity);

        window->SetPresentOffsetY(run->ScreenShakeOffset());
        window->DrawAndRunGraphicsCommands(frame, {});
        run->WaitForFrame();
    }
    run.reset();
    window->Close();
    return 0;
}

} // namespace

int RunLevel(const std::shared_ptr<Ship::Context>& context, const LevelOptions& options) {
    auto window = context->GetChildren().GetFirst<Fast::Fast3dWindow>();
    auto resourceManager = context->GetChildren().GetFirst<Ship::ResourceManager>();

    if (options.demo) {
        return RunDemo(window, resourceManager, options);
    }
    // Before the stage loads, which already draws from the generator.
    RandomSeedFromClock();
    const std::optional<Level::LevelId> id = FindStage(options.stage);
    if (!id) {
        spdlog::error("level view: no stage with bg directory '{}'", options.stage);
        return 1;
    }
    Level::ResourceBgSource source(resourceManager);
    Level::Stage stage(source, &source);
    if (options.players > 1) {
        const Game::MultiplayerSettings settings = Game::MultiplayerSettings::Defaults(options.players);
        const char* reason = nullptr;
        if (!settings.Valid(&reason)) {
            spdlog::error("level view: {} players: {}", options.players, reason);
            return 1;
        }
        stage.SetMultiplayer(settings);
    } else if (options.players != 1) {
        spdlog::error("level view: --players takes 1 to 4, not {}", options.players);
        return 1;
    }
    if (!stage.Load(*id, options.demoSlot, stage.PlayerCount() > 1 ? Game::Save::kDifficultyMulti : 0,
                    options.demoSlot != 0)) {
        spdlog::error("level view: {}", stage.Error());
        return 1;
    }
    const Level::BgLevel& level = stage.Bg();
    if (options.room && (*options.room < 1 || *options.room >= level.MaxNumRooms())) {
        spdlog::error("level view: room {} is not in 1..{}", *options.room, level.MaxNumRooms() - 1);
        return 1;
    }

    // Stands in for the player: a fixed camera at the stage's spawn.
    const bool hasSpawn = !stage.Intro().startPads.empty();
    Level::PlayerPose pose;
    pose.look = options.hasLook ? options.look
                : hasSpawn      ? stage.Spawn().look
                                : std::array<float, 3>{ 0.0f, 0.0f, -1.0f };
    // Every visible room loads on the first frame, and no gun or HUD draws.
    pose.cameraMode = Level::CameraMode::Intro;
    pose.frozen = true;
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
    if (stage.Setup()) {
        const Level::PropCounts& props = stage.PropTotals();
        spdlog::info("level view: {} propdefs: {} objects ({} on this difficulty), {} guards, {} links",
                     stage.Setup()->PropDefs().size(), props.objects, stage.ObjectDefs().size(), props.guards,
                     props.links);
        int solid = 0;
        for (const Level::PlacedObject& object : stage.PlacedObjects().All()) {
            solid += object.collision ? 1 : 0;
        }
        int served = 0;
        for (const size_t def : stage.ObjectDefs()) {
            served += Level::Objects::Serves(stage.Setup()->PropDefs()[def].type) ? 1 : 0;
        }
        spdlog::info("level view: {} of {} plain objects created, {} with collision, {} props active",
                     stage.PlacedObjects().All().size(), served, solid, stage.PlacedObjects().Props().Active().size());
        const auto& chrSlots = stage.PlacedChrs().Slots();
        spdlog::info("level view: {} of {} guards spawned", chrSlots.size() - stage.PlacedChrs().FreeSlots(),
                     stage.PropTotals().guards);
        spdlog::info("level view: {} waypoints in {} groups, {} patrol paths, {} AI lists, {} background characters",
                     stage.Paths().All().size(), stage.Paths().Groups().size(), stage.Paths().Paths().size(),
                     stage.Setup()->AiListSection().size(), stage.PlacedChrs().BackgroundCount());
    }
    const auto& padTiles = stage.PadTiles();
    spdlog::info("level view: pad stan tiles: {} by name, {} by search, {} missing", padTiles.named, padTiles.moved,
                 padTiles.missing);
    Level::Stan& stan = stage.Bg().StanFile();
    if (options.room) {
        pose.room = *options.room;
    } else if (hasSpawn && !options.hasPosition && stage.Spawn().stanTile != Level::kNoTile) {
        pose.room = stan.TileRoom(stage.Spawn().stanTile);
    } else {
        pose.room = RoomAt(stan, pose.position);
    }
    pose.previousPosition = pose.position;
    spdlog::info("level view: {} room {} camera at ({}, {}, {})", options.stage, pose.room, pose.position[0],
                 pose.position[1], pose.position[2]);
    if (hasSpawn) {
        const Level::Pad& pad = stage.Setup()->Pads()[stage.Intro().startPads.front()];
        spdlog::info("level view: start pad at y {}, floor under it {}", pad.position[1], stage.Spawn().stanHeight);
    }

    // Without an explicit camera the controller drives the player from the
    // spawn. A stage whose start found no tile has no player to move, and an
    // explicit --room, --camera or --look asks for the fixed inspection view.
    const bool explicitCamera = options.room || options.hasPosition || options.hasLook;
    const bool live = !explicitCamera && stage.Player().Tile() != Level::kNoTile;
    std::shared_ptr<Ship::ControlDeck> controlDeck;
    if (live) {
        Platform::Input::Init(context);
        controlDeck = context->GetChildren().GetFirst<Ship::ControlDeck>();
        if (options.controlStyle) {
            stage.Options().controlType = *options.controlStyle;
            // The options screen only offers 2.x with two controllers; play
            // then reads the missing second pad as no input.
            if (*options.controlStyle >= Level::ControlConfig::Plenty &&
                Platform::Input::ControllerCount(Platform::Input::ConnectedPortsOf(controlDeck.get())) < 2) {
                spdlog::warn("level view: control style 2.x needs a second controller");
            }
        }
        if (stage.PlayerCount() == 1) {
            stage.EnterIntro();
        }
        pose.cameraMode = stage.CameraPose().cameraMode;
        // The simulation steps one tick per frame, so the frame rate is the
        // tick rate; --uncapped does not apply.
        window->SetTargetFps(Game::kTicksPerSecond);
    } else {
        // A plain stage view loads the swirl's environment.
        stage.LoadFogEnvironment(/*cinema=*/false);
    }
    // A frame has room for every player's view.
    constexpr size_t kFrameCapacity = 16384;
    std::vector<Gfx> frame(kFrameCapacity * static_cast<size_t>(stage.PlayerCount()));
    Display::SetLivePlay(true);
    while (window->IsRunning()) {
        window->HandleEvents();
        Display::SyncDisplaySize(*window);
        stage.SetDisplayWiden(Display::DisplayWiden());
        if (live) {
            // Once per displayed frame, so the pressed-this-frame reads see
            // exactly this frame's new presses.
            Platform::Input::Poll(controlDeck.get());
            // The frame order of a demo run: the clock, the pressed buttons,
            // the stage's tick, then the players' moves.
            stage.TickLive(Platform::Input::Current(), Platform::Input::ControllerCount());
            pose = stage.CameraPose();
            if (stage.PlayerCount() > 1 && stage.Clock().stageTicks % 120 == 0) {
                for (int player = 0; player < stage.PlayerCount(); player++) {
                    const Level::PlayerState& state = stage.PlayerAt(player);
                    spdlog::info("level view: tick {} player {} at ({:.0f}, {:.0f}, {:.0f}) health {:.2f}",
                                 stage.Clock().stageTicks, player + 1, state.move.CollisionPosition()[0],
                                 state.move.CollisionPosition()[1], state.move.CollisionPosition()[2],
                                 state.health.Health());
                }
            }
            // The watch's abort and aiEndLevel end the stage.
            if (stage.LevelEndRequested()) {
                spdlog::info("level view: the stage ended after {} ticks", stage.Clock().stageTicks);
                break;
            }
        } else {
            stage.Tick();
        }

        Gfx* dl = ClearFrame(frame.data());
        dl = stage.PlayerCount() > 1 ? stage.Render(dl, resourceManager) : stage.Render(dl, resourceManager, pose);
        *dl++ = F3dex::EndDisplayList();
        assert(static_cast<size_t>(dl - frame.data()) <= frame.size());

        window->SetPresentOffsetY(stage.ScreenShakeOffset());
        window->DrawAndRunGraphicsCommands(frame.data(), {});
    }

    stage.Unload();
    window->Close();
    return 0;
}

} // namespace Frigate::Viewer
