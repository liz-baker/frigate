#include "game/Game.h"

#include <cassert>
#include <chrono>
#include <cstdint>

#include "fast/Fast3dWindow.h"
#include "fast/interpreter.h"
#include "game/FrontState.h"
#include "game/MenuStateMachine.h"
#include "game/ScreenSize.h"
#include "game/menu/BasicGeometry.h"
#include "game/menu/CastScreen.h"
#include "game/DemoRun.h"
#include "game/menu/EyeIntro.h"
#include "game/menu/FileSelect.h"
#include "game/menu/ModeSelect.h"
#include "game/menu/NoControllers.h"
#include "game/menu/MissionSelect.h"
#include "game/menu/RunStage.h"
#include "game/save/FileSaveStorage.h"
#include "game/save/GameSettings.h"
#include "game/save/SaveManager.h"
#include "game/save/SaveProgress.h"
#include "game/menu/GoldenEyeLogo.h"
#include "game/menu/IntroState.h"
#include "game/menu/LegalScreen.h"
#include "game/menu/NintendoLogo.h"
#include "game/menu/RarewareLogo.h"
#include "gfx/GbiF3dex.h"
#include "model/Random.h"
#include "platform/Input.h"
#include "gfx/TextureCache.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/core/Context.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game {

namespace {

// clear_framebuffer_black()'s opcode sequence (fresh wording). VI-driven
// viewport setup (viSetXY() et al.) isn't wired up yet, so the scissor
// rect and 440x330 size are hardcoded (ScreenSize.h) rather than read from it. The color
// image targets a placeholder address, matching Viewer.cpp's BuildClear().
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

MenuStateMachine BuildMenuStateMachine(const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                                       const Options& options, std::shared_ptr<Menu::RunStageHost>& stageHost) {
    auto front = std::make_shared<FrontState>();
    front->demoOrder = options.demoOrder;
    front->save = std::make_shared<Save::SaveManager>(std::make_shared<Save::FileSaveStorage>());
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
    Menu::RegisterNoControllers(menu, resourceManager);
    Menu::RegisterMissionSelect(menu, resourceManager, front);
    Menu::RegisterRunStage(menu, stageHost);
    return menu;
}

} // namespace

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

    // boss.c seeds the generator with osGetCount(), the CPU's cycle counter
    // (46.875 MHz), when the game starts.
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    RandomSetSeed(static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count() * 3 / 64));

    std::shared_ptr<Menu::RunStageHost> stageHost;
    MenuStateMachine menu = BuildMenuStateMachine(resourceManager, options, stageHost);
    // reload=true: the legal screen appears only after the same
    // SwitchScreens blackout every transition goes through, matching the
    // decomp's cold-boot path.
    menu.ChangeMenu(MenuId::LegalScreen, /*reload=*/true);

    Platform::Input::Init(context);
    auto controlDeck = context->GetChildren().GetFirst<Ship::ControlDeck>();

    bool inStage = false;
    while (window->IsRunning()) {
        window->HandleEvents();
        // One sample per displayed frame, so the menus' and stage's
        // pressed-this-frame reads see exactly this frame's new presses.
        Platform::Input::Poll(controlDeck.get());
        // The frame about to be drawn lasts ClockTimer() ticks.
        if (!options.uncapped) {
            window->SetTargetFps(kTicksPerSecond / menu.ClockTimer());
        }
        menu.Tick();

        // A stage draws at its own resolution, and a demo paces itself against
        // its recording's clock rather than the menus' per-screen frame length.
        if (stageHost->Active() != inStage) {
            inStage = stageHost->Active();
            if (auto interpreter = window->GetInterpreterWeak().lock()) {
                interpreter->SetNativeDimensions(inStage ? kStageScreenWidth : kMenuScreenWidth,
                                                 inStage ? kStageScreenHeight : kMenuScreenHeight);
            }
            if (inStage && !options.uncapped) {
                window->SetTargetFps(kTicksPerSecond);
            }
        }

        // NintendoLogo's LOD distance culling can activate hundreds of
        // lists at once, the legal screen's text is ~11 commands per glyph, and
        // the gun barrel's sight backdrop is 11 per row of 299; sized with
        // headroom, and the assert below catches an overflow instead of
        // silently corrupting the stack.
        constexpr size_t kFrameCapacity = 16384;
        Gfx frame[kFrameCapacity];
        if (inStage) {
            Gfx* stageDl = stageHost->Render(frame);
            *stageDl++ = Frigate::F3dex::EndDisplayList();
            assert(static_cast<size_t>(stageDl - frame) <= kFrameCapacity);
            window->DrawAndRunGraphicsCommands(frame, {});
            stageHost->WaitForFrame();
            continue;
        }
        Gfx* dl = ClearFramebufferBlack(frame);
        // lvlRender() starts every front-end frame with these two lists; the
        // screens' draws rely on the state they set.
        dl = Menu::FastPipelineSetup(dl);
        dl = Menu::ZBufferGeometry(dl);
        dl = menu.Constructor(dl);
        *dl++ = Frigate::F3dex::EndDisplayList();
        assert(static_cast<size_t>(dl - frame) <= kFrameCapacity);

        window->DrawAndRunGraphicsCommands(frame, {});
    }

    window->Close();
    return 0;
}

} // namespace Frigate::Game
