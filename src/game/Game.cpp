#include "game/Game.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "fast/Fast3dWindow.h"
#include "fast/interpreter.h"
#include "diag/Prof.h"
#include "fast/profiler.h"
#include "game/DevCheats.h"
#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/ScreenSize.h"
#include "game/menu/BasicGeometry.h"
#include "game/menu/BriefingScreen.h"
#include "game/menu/CastScreen.h"
#include "game/DemoRun.h"
#include "game/menu/EyeIntro.h"
#include "game/menu/DifficultyScreen.h"
#include "game/menu/CheatMenu.h"
#include "game/menu/FileSelect.h"
#include "game/menu/FileSelectModel.h"
#include "game/menu/ModeSelect.h"
#include "game/menu/MpCharSelectScreen.h"
#include "game/menu/MpControlStyleScreen.h"
#include "game/menu/MpHandicapScreen.h"
#include "game/menu/MpOptionsScreen.h"
#include "game/menu/MpScenarioSelectScreen.h"
#include "game/menu/MpStageSelectScreen.h"
#include "game/menu/MpTeamsScreen.h"
#include "game/menu/NoControllers.h"
#include "game/menu/MenuCheats.h"
#include "game/menu/MissionSelect.h"
#include "game/menu/RunStage.h"
#include "game/menu/StageStart.h"
#include "game/MultiplayerQuickStart.h"
#include "game/save/ActiveSave.h"
#include "game/save/FileSaveStorage.h"
#include "game/save/GameSettings.h"
#include "game/save/SaveManager.h"
#include "game/save/SaveProgress.h"
#include "game/menu/GoldenEyeLogo.h"
#include "game/menu/MissionEnd.h"
#include "game/menu/IntroState.h"
#include "game/menu/LegalScreen.h"
#include "game/menu/NintendoLogo.h"
#include "game/menu/RarewareLogo.h"
#include "gfx/DisplayAspect.h"
#include "gfx/GbiF3dex.h"
#include "level/Stage.h"
#include "model/Random.h"
#include "platform/FrameRate.h"
#include "platform/Input.h"
#include "gfx/TextureCache.h"
#include "resource/factory/DisplayListFactory.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/core/Context.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game {

namespace {

// clear_framebuffer_black()'s opcode sequence. The game's VI viewport setup
// (viSetXY() et al.) has no counterpart: the scissor rect and 440x330 size
// come from ScreenSize.h. The color image targets a placeholder address,
// matching Viewer.cpp's BuildClear().
Gfx* ClearFramebufferBlack(Gfx* dl) {
    using namespace Frigate::F3dex;
    constexpr int32_t kWidth = kMenuScreenWidth;
    constexpr int32_t kHeight = kMenuScreenHeight;

    gDPSetColorImage(dl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, kWidth, reinterpret_cast<void*>(uintptr_t{ 0x00100000 }));
    gDPSetScissor(dl++, G_SC_NON_INTERLACE, 0, 0, kWidth, kHeight);
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_FILL);
    // No depth update, or the fill leaves nearest-depth values behind and
    // every later depth-tested draw fails its z-test against them --
    // drawing with no error but never visible.
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_NOOP | G_RM_NOOP2);
    gDPSetFillColor(dl++, (GPACK_RGBA5551(0, 0, 0, 1) << 16) | GPACK_RGBA5551(0, 0, 0, 1));
    gDPFillRectangle(dl++, 0, 0, kWidth - 1, kHeight - 1);
    gDPPipeSync(dl++);
    return dl;
}

std::atomic<bool> gResetRequested{ false };

std::mutex gPendingMutex;
std::optional<MultiplayerSettings> gPendingMultiplayer;

std::optional<MultiplayerSettings> TakePendingMultiplayer() {
    const std::lock_guard<std::mutex> lock(gPendingMutex);
    return std::exchange(gPendingMultiplayer, std::nullopt);
}

MenuStateMachine BuildMenuStateMachine(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                       const Options& options, std::shared_ptr<Menu::RunStageHost>& stageHost,
                                       MenuId& startMenu) {
    auto front = std::make_shared<FrontState>();
    front->demoOrder = options.demoOrder;
    front->save = std::make_shared<Save::SaveManager>(std::make_shared<Save::FileSaveStorage>());
    Save::SetActiveSaveManager(front->save);
    front->progress = std::make_shared<Save::SaveProgress>(front->save);
    front->settings = std::make_shared<Save::GameSettings>();
    front->progress->SetSettingsCapture(
        [settings = front->settings](Save::SaveData& save) { Save::CaptureSettings(*settings, save); });
    DemoRunOptions demoOptions;
    demoOptions.fadeOnRandomMismatch = options.fadeOnRandomMismatch;
    demoOptions.realtime = !options.uncapped;
    stageHost = std::make_shared<Menu::RunStageHost>(resourceManager, front, demoOptions);
    auto intro = std::make_shared<IntroState>();
    MenuStateMachine menu;
    Menu::RegisterLegalScreen(menu, resourceManager, front);
    Menu::RegisterNintendoLogo(menu, resourceManager, front);
    Menu::RegisterRarewareLogo(menu, resourceManager, front, intro);
    Menu::RegisterEyeIntro(menu, resourceManager, front, intro);
    Menu::RegisterGoldenEyeLogo(menu, resourceManager, front);
    Menu::RegisterCastScreen(menu, resourceManager, front);
    Menu::RegisterFileSelect(menu, resourceManager, front);
    Menu::RegisterModeSelect(menu, resourceManager, front);
    Menu::RegisterMpOptions(menu, resourceManager, front);
    Menu::RegisterMpCharSelect(menu, resourceManager, front);
    Menu::RegisterMpScenarioSelect(menu, resourceManager, front);
    Menu::RegisterMpStageSelect(menu, resourceManager, front);
    Menu::RegisterMpHandicap(menu, resourceManager, front);
    Menu::RegisterMpControlStyle(menu, resourceManager, front);
    Menu::RegisterMpTeams(menu, resourceManager, front);
    Menu::RegisterNoControllers(menu, resourceManager);
    Menu::RegisterMissionSelect(menu, resourceManager, front);
    Menu::RegisterDifficulty(menu, resourceManager, front);
    Menu::RegisterBriefing(menu, resourceManager, front);
    Menu::RegisterMissionEnd(menu, resourceManager, front);
    Menu::RegisterCheatMenu(menu, resourceManager, front);
    Menu::RegisterRunStage(menu, stageHost);
    // lvlManageMpGame's title branch: cheat_buttons_mp_related, then menu_init.
    menu.SetBeforeTick([front, host = stageHost] {
        const Input::Sample pad = Input::ReadPlayer1();
        Menu::TickMenuCheatInput(*front, pad.held, pad.pressed, !host->Active());
        // Modern-only departure: the mouse pointer sets the cursor before the screen
        // reads it, so a click hits what the pointer is over.
        if (!host->Active()) {
            Menu::FileSelectModel::MoveCursorToPointer(pad, front->cursor);
        }
    });
    startMenu = options.missionEnd ? Menu::PrepareMissionEndPreview(*front, *options.missionEnd) : MenuId::LegalScreen;
    if (options.multiplayerStart) {
        MultiplayerSettings settings = *options.multiplayerStart;
        ResolveRandomStage(settings, RandomGetNext());
        if (Menu::SelectMultiplayerStage(*front, settings)) {
            front->quickStart = true;
            startMenu = MenuId::RunStage;
        }
    }
    return menu;
}

} // namespace

void InstallProfilerHooks() {
#ifdef FAST3D_PROFILE
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER0, "events+input");
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER1, "menu_tick");
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER2, "dl_build");
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER3, "wait_for_frame");
    // Inside dl_build: the stage's render pass, in the order it runs (src/diag/Prof.h).
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER4, "r_setup");
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER5, "r_sky");
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER6, "r_props+chr_tick");
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER7, "r_ticks");
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER8, "r_room_draw");
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER9, "r_effects");
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER10, "r_guns_watch");
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER11, "r_hud");
    Fast::Prof::SetUserCounterName(Fast::Prof::COUNTER_USER_A, "onscreen_props");
    Fast::Prof::SetUserExperimentName(0, "skip_effects");
    Fast::Prof::SetUserExperimentName(1, "skip_sky");
    Fast::Prof::SetUserExperimentName(2, "skip_guns_hud");
    Fast::Prof::SetUserExperimentName(3, "skip_ticks");
    // The allocator's paths (platform/WiiUAllocator.cpp): small pooled blocks, plain mallocs of
    // medium and large ones, and the optional pool for the medium ones.
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER12, "new_small");
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER13, "malloc_large");
    Fast::Prof::SetUserStageName(Fast::Prof::STAGE_USER14, "pool_medium");
    Fast::Prof::SetUserCounterName(Fast::Prof::COUNTER_USER_B, "new_small");
    Fast::Prof::SetUserCounterName(Fast::Prof::COUNTER_USER_C, "new_medium_pooled");
    Fast::Prof::SetUserCounterName(Fast::Prof::COUNTER_USER_D, "new_large");
    Diag::gHooks.enter = [](int stage) {
        Fast::Prof::Enter(static_cast<Fast::Prof::Stage>(Fast::Prof::STAGE_USER4 + stage));
    };
    Diag::gHooks.exit = [] { Fast::Prof::Exit(); };
    Diag::gHooks.replace = [](int stage) {
        Fast::Prof::Switch(static_cast<Fast::Prof::Stage>(Fast::Prof::STAGE_USER4 + stage));
    };
    Diag::gHooks.experiment = [](int experiment) {
        const Fast::Prof::Experiments& e = Fast::Prof::gExp;
        return e.user[experiment];
    };
    Diag::gHooks.count = [](int counter, uint64_t n) {
        Fast::Prof::Count(static_cast<Fast::Prof::Counter>(Fast::Prof::COUNTER_USER_A + counter), n);
    };
#endif
}

int Run(const std::shared_ptr<Ship::Context>& context, const Options& options) {
    auto window = context->GetChildren().GetFirst<Fast::Fast3dWindow>();
    auto resourceManager = context->GetChildren().GetFirst<Ship::ResourceManager>();

    // Scissor, viewport and texture-rectangle coordinates are all in
    // viSetXY()'s framebuffer space, not the interpreter's default 320x240.
    if (auto interpreter = window->GetInterpreterWeak().lock()) {
        interpreter->SetNativeDimensions(kMenuScreenWidth, kMenuScreenHeight);
    }
    TextureCache::Invalidator() = [weak = window->GetInterpreterWeak()](const void* address) {
        if (auto interpreter = weak.lock()) {
            interpreter->TextureCacheDelete(static_cast<const uint8_t*>(address));
        }
    };

    RandomSeedFromClock();

    std::shared_ptr<Menu::RunStageHost> stageHost;
    // What the boot builds from: a reset drops the multiplayer start, and a
    // requested one replaces it.
    Options current = options;
    // reload=true: the legal screen appears only after the same
    // SwitchScreens blackout every transition goes through, matching the
    // decomp's cold-boot path.
    const auto boot = [&](bool isReset) {
        MenuId start = MenuId::LegalScreen;
        MenuStateMachine booted = BuildMenuStateMachine(resourceManager, current, stageHost, start);
        if (isReset) {
            start = MenuId::LegalScreen;
        }
        // The players a quick-started game has are driven whether or not a
        // controller is attached for each.
        Platform::Input::SessionForcedPorts() =
            current.multiplayerStart ? PortsForPlayers(current.multiplayerStart->playerCount) : 0;
        if (current.multiplayerStart) {
            spdlog::info("game: multiplayer quick start, {} players, controller ports {:#x} forced connected",
                         current.multiplayerStart->playerCount, Platform::Input::SessionForcedPorts());
        }
        booted.SetTransitionObserver([](uint32_t tick, MenuId from, MenuId to) {
            spdlog::info("menu: tick {} {} -> {}", tick, static_cast<int>(from), static_cast<int>(to));
        });
        booted.SwitchMenu(start, /*reload=*/true);
        return booted;
    };
    MenuStateMachine menu = boot(/*isReset=*/false);

    Platform::Input::Init(context);
    // Modern input reads the stage's state for the player on each port; the host
    // is rebuilt on a reset, so it is looked up each frame.
    Platform::Input::SetModernContextProvider([&stageHost](int port) {
        return stageHost ? stageHost->ModernContextOf(port) : Platform::Input::ModernContext{};
    });
    auto controlDeck = context->GetChildren().GetFirst<Ship::ControlDeck>();

    auto cvars = context->GetChildren().GetFirst<Ship::ConsoleVariable>();
    const auto limited = [&cvars](int fps) {
        return Platform::LimitedFps(fps, cvars != nullptr ? cvars->GetInteger(Platform::kFpsLimitCVar, 0) : 0);
    };

    // NintendoLogo's LOD distance culling can activate hundreds of lists at
    // once, the legal screen's text is ~11 commands per glyph, and the gun
    // barrel's sight backdrop is 11 per row of 299; a frame is sized with
    // headroom for that, once for each player a stage draws a view for, and
    // the asserts catch an overflow instead of silently corrupting the heap.
    constexpr size_t kFrameCapacity = 16384;
    std::vector<Gfx> frameStore(kFrameCapacity * Level::Stage::kMaxPlayers);
    bool inStage = false;
    int32_t nativeWidth = kStageScreenWidth;
    int32_t nativeHeight = kStageScreenHeight;
    // Menus, demos and cutscenes are 4:3 whatever the Aspect Ratio setting is.
    Display::SetLivePlay(false, /*force=*/true);
    InstallProfilerHooks();
    while (window->IsRunning()) {
        {
            PROF_SCOPE(USER0);
            window->HandleEvents();
            // One sample per displayed frame, so the menus' and stage's
            // pressed-this-frame reads see exactly this frame's new presses.
            Platform::Input::Poll(controlDeck.get());
        }
        if (gResetRequested.exchange(false)) {
            spdlog::info("game: reset");
            current.multiplayerStart.reset();
            // A display list is expanded once, when first loaded, from the Mip-Mapped Textures setting; drop the
            // model and room lists so the ones that load next use the current setting.
            UnloadExpandedDisplayLists(*resourceManager);
            menu = boot(/*isReset=*/true);
        }
        if (auto pending = TakePendingMultiplayer()) {
            spdlog::info("game: starting a {} player multiplayer game", pending->playerCount);
            current.multiplayerStart = pending;
            menu = boot(/*isReset=*/false);
        }
        Display::SyncDisplaySize(*window);
        // The frame about to be drawn lasts ClockTimer() ticks.
        if (!options.uncapped) {
            window->SetTargetFps(limited(kTicksPerSecond / menu.ClockTimer()));
        }
        {
            PROF_SCOPE(USER1);
            menu.Tick();
            DevCheats::Sync(stageHost->PlayedStage());
        }

        // Switched here, between frames, so the framebuffer is reshaped before
        // the next frame is drawn (the stage frame that follows a load, and the
        // menu frame that follows a mission's end).
        Display::SetLivePlay(stageHost->WidensDisplay());

        // A stage draws at its own resolution, and a demo paces itself against
        // its recording's clock rather than the menus' per-screen frame length.
        const auto [stageWidth, stageHeight] = stageHost->ScreenSize();
        if (stageHost->Active() != inStage || (inStage && (stageWidth != nativeWidth || stageHeight != nativeHeight))) {
            inStage = stageHost->Active();
            nativeWidth = stageWidth;
            nativeHeight = stageHeight;
            window->SetPresentOffsetY(0);
            if (auto interpreter = window->GetInterpreterWeak().lock()) {
                interpreter->SetNativeDimensions(inStage ? nativeWidth : kMenuScreenWidth,
                                                 inStage ? nativeHeight : kMenuScreenHeight);
            }
            if (inStage && !options.uncapped) {
                window->SetTargetFps(limited(kTicksPerSecond));
            }
        }

        Gfx* const frame = frameStore.data();
        if (inStage) {
            {
                PROF_SCOPE(USER2);
                Gfx* stageDl = stageHost->Render(frame);
                *stageDl++ = Frigate::F3dex::EndDisplayList();
                assert(static_cast<size_t>(stageDl - frame) <= frameStore.size());
                window->SetPresentOffsetY(stageHost->ScreenShakeOffset());
            }
            window->DrawAndRunGraphicsCommands(frame, {});
            {
                PROF_SCOPE(USER3);
                stageHost->WaitForFrame();
            }
            continue;
        }
        {
            PROF_SCOPE(USER2);
            Gfx* dl = ClearFramebufferBlack(frame);
            // lvlRender() starts every front-end frame with these two lists; the
            // screens' draws rely on the state they set.
            dl = Menu::FastPipelineSetup(dl);
            dl = Menu::ZBufferGeometry(dl);
            dl = menu.Constructor(dl);
            *dl++ = Frigate::F3dex::EndDisplayList();
            assert(static_cast<size_t>(dl - frame) <= kFrameCapacity);
        }

        window->DrawAndRunGraphicsCommands(frame, {});
    }

    Platform::Input::SetModernContextProvider({});
    window->Close();
    return 0;
}

void RequestReset() {
    gResetRequested = true;
}

void RequestMultiplayerStart(const MultiplayerSettings& settings) {
    const std::lock_guard<std::mutex> lock(gPendingMutex);
    gPendingMultiplayer = settings;
}

} // namespace Frigate::Game
