#include "game/Game.h"

#include <cassert>
#include <cstdint>

#include "fast/Fast3dWindow.h"
#include "fast/interpreter.h"
#include "game/FrontState.h"
#include "game/MenuStateMachine.h"
#include "game/ScreenSize.h"
#include "game/menu/EyeIntro.h"
#include "game/menu/GoldenEyeLogo.h"
#include "game/menu/IntroState.h"
#include "game/menu/LegalScreen.h"
#include "game/menu/NintendoLogo.h"
#include "game/menu/RarewareLogo.h"
#include "gfx/GbiF3dex.h"
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

MenuStateMachine BuildMenuStateMachine(const std::shared_ptr<Ship::ResourceManager>& resourceManager) {
    auto front = std::make_shared<FrontState>();
    auto intro = std::make_shared<IntroState>();
    MenuStateMachine menu;
    Menu::RegisterLegalScreen(menu, resourceManager);
    Menu::RegisterNintendoLogo(menu, resourceManager, front);
    Menu::RegisterRarewareLogo(menu, resourceManager, front, intro);
    Menu::RegisterEyeIntro(menu, resourceManager, front, intro);
    Menu::RegisterGoldenEyeLogo(menu, resourceManager, front);
    return menu;
}

} // namespace

int Run(const std::shared_ptr<Ship::Context>& context) {
    auto window = context->GetChildren().GetFirst<Fast::Fast3dWindow>();
    auto resourceManager = context->GetChildren().GetFirst<Ship::ResourceManager>();

    // Scissor, viewport and texture-rectangle coordinates are all in
    // viSetXY()'s framebuffer space, not the interpreter's default 320x240.
    if (auto interpreter = window->GetInterpreterWeak().lock()) {
        interpreter->SetNativeDimensions(kMenuScreenWidth, kMenuScreenHeight);
    }

    MenuStateMachine menu = BuildMenuStateMachine(resourceManager);
    // reload=true: the legal screen appears only after the same
    // SwitchScreens blackout every transition goes through, matching the
    // decomp's cold-boot path.
    menu.ChangeMenu(MenuId::LegalScreen, /*reload=*/true);

    while (window->IsRunning()) {
        window->HandleEvents();
        menu.Tick();

        // NintendoLogo's LOD distance culling can activate hundreds of
        // lists at once, the legal screen's text is ~11 commands per glyph, and
        // the gun barrel's sight backdrop is 11 per row of 299; sized with
        // headroom, and the assert below catches an overflow instead of
        // silently corrupting the stack.
        constexpr size_t kFrameCapacity = 16384;
        Gfx frame[kFrameCapacity];
        Gfx* dl = ClearFramebufferBlack(frame);
        dl = menu.Constructor(dl);
        *dl++ = Frigate::F3dex::EndDisplayList();
        assert(static_cast<size_t>(dl - frame) <= kFrameCapacity);

        window->DrawAndRunGraphicsCommands(frame, {});
    }

    window->Close();
    return 0;
}

} // namespace Frigate::Game
