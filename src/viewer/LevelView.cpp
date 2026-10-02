#include "viewer/LevelView.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cassert>
#include <optional>
#include <thread>

#include <spdlog/spdlog.h>

#include "fast/Fast3dWindow.h"
#include "game/FrameTiming.h"
#include "gfx/GbiF3dex.h"
#include "level/DemoPlayback.h"
#include "level/ResourceBgSource.h"
#include "level/ResourceDemoSource.h"
#include "level/Stage.h"
#include "model/Random.h"
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

// The room of the stan tile nearest `position`, which is how the game places a
// player without a start pad; room 1 when the stage has no tiles.
int RoomAt(Level::Stan& stan, std::array<float, 3> position) {
    const int tile = stan.FindNearestWalkable(&position, 0.0f);
    return tile == Level::kNoTile ? 1 : stan.TileRoom(tile);
}

// The camera the player's state gives: at its position, looking along the
// applied view the head's motion tilts.
Level::PlayerPose PlayerPoseOf(const Level::PlayerMove& player) {
    Level::PlayerPose pose;
    pose.position = player.Position();
    pose.previousPosition = player.PreviousPosition();
    pose.look = player.AppliedView();
    pose.up = player.AppliedViewUp();
    pose.room = player.Room();
    return pose;
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

    std::optional<Level::DemoPlayback> playback;
    std::optional<Level::LevelId> id;
    std::optional<Level::PlayerOptions> demoOptions;
    int demoSlot = options.demoSlot;
    int difficulty = 0;
    if (options.demo) {
        Level::ResourceDemoSource demos(resourceManager);
        const auto selected =
            *options.demo >= 0 ? Level::LoadAttractDemo(demos, static_cast<size_t>(*options.demo)) : std::nullopt;
        const auto file = selected ? demos.LoadDemo(selected->name) : std::nullopt;
        if (!file) {
            spdlog::error("level view: no demo {}", *options.demo);
            return 1;
        }
        playback = Level::DemoPlayback::Parse(*file);
        if (!playback) {
            spdlog::error("level view: demo '{}' is truncated", selected->name);
            return 1;
        }
        id = selected->header.stage;
        demoSlot = selected->header.slot;
        difficulty = selected->header.difficulty;
        // copy_recorded_ramrom_registers_to_proper_place_ingame, before the
        // stage loads.
        RandomSetState(selected->header.randomSeed);
        ChrObjRandomSetState(selected->header.chrObjRandomSeed);
        // fileLoadSettingsForFolder, on the save file the demo carries.
        demoOptions = Level::PlayerOptions::FromSaveBits(selected->header.options);
        spdlog::info("level view: demo {} '{}'", *options.demo, selected->name);
    } else {
        id = FindStage(options.stage);
        if (!id) {
            spdlog::error("level view: no stage with bg directory '{}'", options.stage);
            return 1;
        }
    }
    Level::ResourceBgSource source(resourceManager);
    Level::Stage stage(source, &source);
    if (!stage.Load(*id, demoSlot, difficulty)) {
        spdlog::error("level view: {}", stage.Error());
        return 1;
    }
    if (demoOptions) {
        stage.Options() = *demoOptions;
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

    // A demo skips the intro, so it passes through the swirl on its way to
    // first person; a plain stage view loads the swirl's environment too.
    stage.EnterSwirl();
    if (playback) {
        // Demos start in first person.
        stage.EnterFirstPerson();
    }
    bool finished = false;
    int packets = 0;
    int seedMatches = 0;
    int32_t watchState = 0;
    // The frames the recording has run, which paces the replay: the stage's own
    // clock stands still while the watch is open.
    int32_t demoTicks = 0;
    // Paces demo playback against wall time directly, anchored to when it
    // started: SetTargetFps only takes an integer fps, so deriving one from
    // speedFrames each packet (kTicksPerSecond / speedFrames) truncates for
    // any speedFrames that doesn't divide 60 evenly and drifts slow over the
    // length of the demo.
    const auto demoStart = std::chrono::steady_clock::now();
    while (window->IsRunning()) {
        window->HandleEvents();
        if (playback && !finished) {
            if (const auto packet = playback->Next()) {
                // ramrom_replay_handler fades to the title when the recorded
                // random byte differs from the game's; that needs every random
                // draw the guards make (#109), so only report it.
                packets++;
                demoTicks += packet->speedFrames;
                if (packet->randSeed == static_cast<uint8_t>(RandomState())) {
                    seedMatches++;
                } else if (packets - seedMatches == 1) {
                    spdlog::info("level view: random state first differs from the recording at packet {} (tick {})",
                                 packets, stage.Clock().stageTicks);
                }
                stage.SetClockTimer(packet->speedFrames);
                stage.Tick();
                stage.MovePlayer(packet->Input());
                if (stage.LevelEndRequested()) {
                    // aiEndLevel returns to the title.
                    spdlog::info("level view: an AI list ended the level after {} ticks", stage.Clock().stageTicks);
                    finished = true;
                }
                if (stage.GetWatch().State() != watchState) {
                    spdlog::info("level view: watch state {} -> {} at tick {}", watchState, stage.GetWatch().State(),
                                 stage.Clock().stageTicks);
                    watchState = stage.GetWatch().State();
                }
                if (stage.Clock().stageTicks / 120 != (stage.Clock().stageTicks - stage.Clock().clockTimer) / 120) {
                    const Level::PlayerMove& player = stage.Player();
                    spdlog::info(
                        "level view: tick {} at ({:.1f}, {:.1f}, {:.1f}) room {} floor {:.1f} heading {:.1f} weapon {}",
                        stage.Clock().stageTicks, player.CollisionPosition()[0], player.CollisionPosition()[1],
                        player.CollisionPosition()[2], player.Room(), player.StanHeight(), player.Theta(),
                        stage.Weapons().Ready() ? stage.Weapons().WeaponId(Level::HandRight) : -1);
                }
                pose = PlayerPoseOf(stage.Player());
                pose.cameraMode = Level::CameraMode::FirstPerson;
            } else {
                // The game fades to the title here.
                spdlog::info("level view: demo finished after {} ticks; random byte matched {} of {} packets",
                             stage.Clock().stageTicks, seedMatches, packets);
                finished = true;
            }
        } else if (!playback) {
            stage.Tick();
        }

        constexpr size_t kFrameCapacity = 16384;
        Gfx frame[kFrameCapacity];
        Gfx* dl = ClearFrame(frame);
        dl = stage.Render(dl, resourceManager, pose);
        *dl++ = F3dex::EndDisplayList();
        assert(static_cast<size_t>(dl - frame) <= kFrameCapacity);

        window->DrawAndRunGraphicsCommands(frame, {});

        if (playback && !finished) {
            const auto target =
                demoStart +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(static_cast<double>(demoTicks) / Frigate::Game::kTicksPerSecond));
            std::this_thread::sleep_until(target);
        }
    }

    stage.Unload();
    window->Close();
    return 0;
}

} // namespace Frigate::Viewer
