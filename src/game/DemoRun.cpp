#include "game/DemoRun.h"

#include <thread>

#include <spdlog/spdlog.h>

#include "game/FrameTiming.h"
#include "gfx/GbiF3dex.h"
#include "level/AttractDemo.h"
#include "level/ResourceDemoSource.h"
#include "model/Random.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game {

namespace {

// Frames the stage keeps running after the recording ran out, waiting on the
// fade to finish, before the demo is ended regardless: the fade is paced by
// the stage's clock, which stands still while the watch is open.
constexpr int kEndFadeFrames = 120;

} // namespace

Gfx* ClearStageFrame(Gfx* dl, int32_t width, int32_t height) {
    using namespace Frigate::F3dex;
    gDPSetColorImage(dl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, width, reinterpret_cast<void*>(uintptr_t{ 0x00100000 }));
    gDPSetScissor(dl++, G_SC_NON_INTERLACE, 0, 0, width, height);
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_FILL);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_NOOP | G_RM_NOOP2);
    const uint32_t fill = GPACK_RGBA5551(0, 0, 0, 1);
    gDPSetFillColor(dl++, (fill << 16) | fill);
    gDPFillRectangle(dl++, 0, 0, width - 1, height - 1);
    gDPPipeSync(dl++);
    return dl;
}

Level::PlayerPose StagePlayerStartPose(Level::Stage& stage) {
    Level::PlayerPose pose;
    if (!stage.Intro().startPads.empty()) {
        pose.look = stage.Spawn().look;
        pose.position = stage.Spawn().EyePosition();
        if (stage.Spawn().stanTile != Level::kNoTile) {
            pose.room = stage.Bg().StanFile().TileRoom(stage.Spawn().stanTile);
        }
    }
    pose.previousPosition = pose.position;
    return pose;
}

DemoRun::DemoRun(const std::shared_ptr<Ship::ResourceManager>& resourceManager, const DemoRunOptions& options)
    : resourceManager_(resourceManager), options_(options) {
}

DemoRun::~DemoRun() {
    if (stage_) {
        stage_->Unload();
    }
    // stop_demo_playback.
    RandomSetState(savedRandom_);
    ChrObjRandomSetState(savedChrObjRandom_);
}

std::unique_ptr<DemoRun> DemoRun::Start(const std::shared_ptr<Ship::ResourceManager>& resourceManager, size_t index,
                                        const DemoRunOptions& options) {
    std::unique_ptr<DemoRun> run(new DemoRun(resourceManager, options));

    Level::ResourceDemoSource demos(resourceManager);
    const auto selected = Level::LoadAttractDemo(demos, index);
    const auto file = selected ? demos.LoadDemo(selected->name) : std::nullopt;
    if (!file) {
        spdlog::error("attract: no demo {}", index);
        return nullptr;
    }
    run->playback_ = Level::DemoPlayback::Parse(*file);
    if (!run->playback_) {
        spdlog::error("attract: demo '{}' is truncated", selected->name);
        return nullptr;
    }

    // copy_current_ingame_registers_before_ramrom_playback, then
    // copy_recorded_ramrom_registers_to_proper_place_ingame, before the stage
    // loads.
    run->savedRandom_ = RandomState();
    run->savedChrObjRandom_ = ChrObjRandomState();
    RandomSetState(selected->header.randomSeed);
    ChrObjRandomSetState(selected->header.chrObjRandomSeed);

    run->source_ = std::make_unique<Level::ResourceBgSource>(resourceManager);
    run->stage_ = std::make_unique<Level::Stage>(*run->source_, run->source_.get());
    Level::Stage& stage = *run->stage_;
    if (!stage.Load(selected->header.stage, selected->header.slot, selected->header.difficulty, /*demo=*/true)) {
        spdlog::error("attract: {}", stage.Error());
        run->stage_.reset();
        return nullptr;
    }
    // fileLoadSettingsForFolder, on the save file the demo carries.
    stage.Options() = Level::PlayerOptions::FromSaveBits(selected->header.options);
    spdlog::info("attract: demo {} '{}'", index, selected->name);

    run->pose_ = StagePlayerStartPose(stage);

    // A demo skips the intro camera and the swirl, which the camera modes
    // pass through on their way to first person.
    stage.EnterIntro();
    run->pose_.cameraMode = stage.CameraPose().cameraMode;
    run->start_ = std::chrono::steady_clock::now();
    return run;
}

void DemoRun::Step(bool buttonPressed) {
    if (done_) {
        return;
    }
    Level::Stage& stage = *stage_;
    // A demo is always shown at 4:3, so the stage keeps its default widen of 1
    // and what counts as on screen matches the recording.

    if (const auto packet = playback_->Next()) {
        packets_++;
        demoTicks_ += packet->speedFrames;
        if (packet->randSeed == static_cast<uint8_t>(RandomState())) {
            seedMatches_++;
        } else {
            if (packets_ - seedMatches_ == 1) {
                spdlog::info("attract: random state first differs from the recording at packet {} (tick {})", packets_,
                             stage.Clock().stageTicks);
            }
            if (options_.fadeOnRandomMismatch) {
                stage.FadeToTitle();
            }
        }
        stage.SetClockTimer(packet->speedFrames);

        // iterate_ramrom_entries_handle_camera_out: the fade starts 60 ticks
        // before the recording's total time.
        const int32_t fadeStart = playback_->TotalTime() - 60;
        if (stage.Clock().globalTimer >= fadeStart &&
            stage.Clock().globalTimer - stage.Clock().clockTimer < fadeStart) {
            stage.FadeToTitle();
        }
        stage.SetButtonsPressed(packet->Input().pressed);
        stage.Tick();
        stage.MovePlayer(packet->Input());
    } else {
        if (!streamEnded_) {
            streamEnded_ = true;
            spdlog::info("attract: demo finished after {} ticks; random byte matched {} of {} packets",
                         stage.Clock().stageTicks, seedMatches_, packets_);
            stage.FadeToTitle();
        }
        // No more recorded input: the player stands still while the fade runs out.
        stage.SetClockTimer(1);
        stage.SetButtonsPressed(0);
        stage.Tick();
        stage.MovePlayer({});
        if (++framesAfterEnd_ >= kEndFadeFrames) {
            spdlog::warn("attract: the fade to the title never finished; ending the demo");
            done_ = true;
        }
    }

    // ramrom_replay_handler: a button press fades to the title and sends the
    // front-end to the file select.
    if (buttonPressed) {
        stage.FadeToTitle();
        endedByButton_ = true;
    }

    if (stage.LevelEndRequested()) {
        // aiEndLevel and the watch's abort return to the title at once.
        spdlog::info("attract: the stage ended after {} ticks", stage.Clock().stageTicks);
        done_ = true;
    }
    if (stage.GetWatch().State() != watchState_) {
        spdlog::info("attract: watch state {} -> {} at tick {}", watchState_, stage.GetWatch().State(),
                     stage.Clock().stageTicks);
        watchState_ = stage.GetWatch().State();
    }
    if (stage.FadeToTitleComplete()) {
        spdlog::info("attract: faded to the title after {} ticks; random byte matched {} of {} packets",
                     stage.Clock().stageTicks, seedMatches_, packets_);
        done_ = true;
    }

    pose_ = stage.CameraPose();
}

Gfx* DemoRun::Render(Gfx* dl) {
    dl = ClearStageFrame(dl);
    return stage_->Render(dl, resourceManager_, pose_);
}

void DemoRun::WaitForFrame() const {
    if (!options_.realtime) {
        return;
    }
    // Anchored to the start rather than to a per-frame target fps, which
    // truncates for any frame length that doesn't divide 60 and drifts slow
    // over the length of the demo.
    std::this_thread::sleep_until(
        start_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                     std::chrono::duration<double>(static_cast<double>(demoTicks_) / kTicksPerSecond)));
}

} // namespace Frigate::Game
