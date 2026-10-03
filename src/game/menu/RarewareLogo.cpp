#include "game/menu/RarewareLogo.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <spdlog/spdlog.h>

#include "fast/resource/type/Texture.h"
#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/Sound.h"
#include "game/menu/BasicGeometry.h"
#include "game/menu/IntroState.h"
#include "game/menu/PropCamera.h"
#include "gfx/FixedMtx.h"
#include "gfx/GbiF3dex.h"
#include "gfx/Gu.h"
#include "model/ModelDraw.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

// retrieve_display_rareware_logo()'s NTSC values (PAL uses 58/33915/216/241);
// this port targets NTSC/USA only (see README).
constexpr int kFadeDenominator = 70;
constexpr int kFadeSubtrahend = 40800;
constexpr int kEyeCount1 = 260;
constexpr int kEyeCount2 = 290;

// load_display_rare_logo()'s NTSC spin step in degrees per frame.
constexpr float kSpinStep = 2.0f;

constexpr float kLogoFovY = 60.0f;
constexpr float kLogoAspect = 320.0f / 240.0f;
constexpr float kLogoNear = 100.0f;
constexpr float kLogoFar = 5000.0f;

// The 32x32 RGBA16 images the screen loads from its own code.
constexpr const char* kBackdropImage = "rarewarelogo/img/004fe8";  // D_02004FE8
constexpr const char* kHighlightImage = "rarewarelogo/img/005ff0"; // D_02005FF0

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<IntroState> intro;
    std::shared_ptr<Ship::ResourceManager> resourceManager;

    // Read by address while the interpreter draws the frame.
    Vp_t viewport{};
    Mtx::FixedMtx projection{};
    Mtx::FixedMtx lookAt{};
    Mtx::FixedMtx rotation{};

    // D_020043E8, DL_RAREWARETEXT and D_02004758.
    std::array<std::optional<Model::ListCall>, 3> lists;
    std::shared_ptr<Fast::Texture> backdrop;
    std::shared_ptr<Fast::Texture> highlight;
};

State& GetState() {
    static State state;
    return state;
}

constexpr std::array<const char*, 3> kListPaths = { "rarewarelogo/dl/0043e8", "rarewarelogo/dl/0044b0",
                                                    "rarewarelogo/dl/004758" };

Gfx* AppendList(Gfx* dl, const std::optional<Model::ListCall>& call) {
    if (call) {
        *dl++ = call->commands[0];
        *dl++ = call->commands[1];
    }
    return dl;
}

// gDPLoadTextureBlock() of a 32x32 RGBA16 image, wrapping in both axes with
// 5-bit masks. The SETTIMG carries the image's real row stride rather than the
// macro's 1 (see src/game/TextRender.cpp).
Gfx* LoadImage32(Gfx* dl, const Fast::Texture& image) {
    gDPSetTextureImage(dl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, image.ImageData);
    gDPSetTile(dl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, 5, G_TX_NOLOD,
               G_TX_NOMIRROR | G_TX_WRAP, 5, G_TX_NOLOD);
    gDPLoadSync(dl++);
    gDPLoadBlock(dl++, G_TX_LOADTILE, 0, 0, 32 * 32 - 1, CALC_DXT(32, G_IM_SIZ_16b_BYTES));
    gDPPipeSync(dl++);
    gDPSetTile(dl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, ((32 * G_IM_SIZ_16b_LINE_BYTES) + 7) >> 3, 0, G_TX_RENDERTILE, 0,
               G_TX_NOMIRROR | G_TX_WRAP, 5, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, 5, G_TX_NOLOD);
    gDPSetTileSize(dl++, G_TX_RENDERTILE, 0, 0, (32 - 1) << G_TEXTURE_IMAGE_FRAC, (32 - 1) << G_TEXTURE_IMAGE_FRAC);
    return dl;
}

// load_display_rare_logo(): `cameraZ` is the logo camera's distance and
// `level` the light/prim colour intensity (0-255).
Gfx* DrawLogo(Gfx* dl, State& state, float cameraZ, int level) {
    using namespace Frigate::F3dex;
    IntroState& intro = *state.intro;

    // The framebuffer clear that follows dlBasicGeometry in the decomp is the
    // one the game loop already emits ahead of every constructor.
    dl = BasicGeometry(dl);

    // The viewport comes from the game's VI setup in the decomp, which this
    // port doesn't have. The modelview load also moves ahead of the
    // projection's: the interpreter multiplies a projection load by the top of
    // the modelview stack, which is empty until the first modelview load.
    Gu::MatrixF lookAt;
    Gu::LookAtF(lookAt, 0.0f, 0.0f, cameraZ, 0.0f, 0.0f, cameraZ - 1.0f, 0.0f, 1.0f, 0.0f);
    state.lookAt = Mtx::ToFixedMtx(lookAt);
    *dl++ = Viewport(&state.viewport);
    *dl++ = Matrix(&state.lookAt, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);

    // gSPPerspNormalize(): scales the RSP's fixed-point vertex w, which the
    // interpreter computes in floating point.
    Gu::MatrixF projection;
    Gu::PerspectiveF(projection, kLogoFovY, kLogoAspect, kLogoNear, kLogoFar, 1.0f);
    state.projection = Mtx::ToFixedMtx(projection);
    *dl++ = Matrix(&state.projection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);

    *dl++ = ClearGeometryMode(0xFFFFFFFFu);
    *dl++ =
        SetGeometryMode(G_SHADE | Fast::F3DEX_G_CULL_BACK | G_LIGHTING | G_TEXTURE_GEN | Fast::F3DEX_G_SHADING_SMOOTH);

    Gu::MatrixF rotation;
    Gu::RotateF(rotation, intro.rareLogoAngle, 0.0f, 1.0f, 0.0f);
    state.rotation = Mtx::ToFixedMtx(rotation);
    intro.rareLogoAngle += kSpinStep;
    *dl++ = Matrix(&state.rotation, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_MUL);

    // gSPSetLights1(gunbarrelLights); the ambient colour set below is what
    // the interpreter reads, since it DMAs the lights when it draws.
    gSPNumLights(dl++, NUMLIGHTS_1);
    gSPLight(dl++, &intro.gunbarrelLights.l[0], LIGHT_1);
    gSPLight(dl++, &intro.gunbarrelLights.a, LIGHT_1 + 1);
    const auto ambient = static_cast<uint8_t>(level);
    for (int i = 0; i < 3; i++) {
        intro.gunbarrelLights.a.l.col[i] = ambient;
        intro.gunbarrelLights.a.l.colc[i] = ambient;
    }

    gDPPipeSync(dl++);
    gDPPipeSync(dl++);
    gDPSetCombineMode(dl++, G_CC_MODULATEI, G_CC_MODULATEI);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_PERSP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTDETAIL, 2, G_TD_CLAMP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_TILE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_BILERP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTCONV, 3, G_TC_FILT);
    gDPPipeSync(dl++);
    gDPPipeSync(dl++);
    *dl++ = Texture(0x0800, 0x0800, 0, G_TX_RENDERTILE, 1);

    dl = LoadImage32(dl, *state.backdrop);
    gDPSetPrimColor(dl++, 0, 0, level, level, level, 0xFF);
    dl = AppendList(dl, state.lists[0]);
    dl = AppendList(dl, state.lists[1]);

    dl = LoadImage32(dl, *state.highlight);
    gDPSetPrimColor(dl++, 0, 0, (level * 0xF0) / 0xFF, (level * 0xD0) / 0xFF, (level * 0xF0) / 0xFF, 0xFF);
    dl = AppendList(dl, state.lists[2]);
    return dl;
}

void Init(const std::shared_ptr<Ship::ResourceManager>& resourceManager, MenuStateMachine&) {
    State& state = GetState();
    IntroState& intro = *state.intro;

    // setupRarewareLogoData(). The decomp also copies the logo segment from
    // ROM into the segment-2 buffer here; the packed lists reference their
    // vertices and images by path instead, so there is nothing to copy.
    intro.gunbarrelMode = 0;
    intro.titleX = 880.0f;
    intro.rareLogoAngle = -40.0f;
    intro.introEyeCounter = 0;

    Sound::PlaySfx(AudioDriver::kSfxRarelogo);

    state.viewport = MenuViewport();
    state.resourceManager = resourceManager;
    for (size_t i = 0; i < kListPaths.size(); i++) {
        state.lists[i] = Model::CallList(resourceManager, kListPaths[i]);
    }
    state.backdrop = resourceManager->LoadResource<Fast::Texture>(kBackdropImage);
    state.highlight = resourceManager->LoadResource<Fast::Texture>(kHighlightImage);
    if (state.backdrop == nullptr || state.highlight == nullptr) {
        spdlog::error("rareware logo: images missing from the loaded archives");
    }
}

void Interface(MenuStateMachine& menu) {
    FrontState& front = *GetState().front;
    if (GetState().intro->gunbarrelMode == 2) {
        menu.ChangeMenu(MenuId::EyeIntro, /*reload=*/true);
        return;
    }
    if (Input::AnyButtonPressedThisFrame()) {
        if (!front.isFirstTimeOnMainMenu) {
            menu.ChangeMenu(MenuId::FileSelect, /*reload=*/true);
            return;
        }
        front.prevKeypresses = true;
        menu.ChangeMenu(MenuId::EyeIntro, /*reload=*/true);
    }
}

Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    IntroState& intro = *state.intro;
    if (state.backdrop == nullptr || state.highlight == nullptr) {
        return dl;
    }

    if (intro.gunbarrelMode == 0 || intro.gunbarrelMode == 1) {
        const int counter = intro.introEyeCounter;
        const int fadeIn = std::clamp((counter * 255) / kFadeDenominator, 0, 255);
        const int fadeOut = std::clamp(255 - (((counter * 255) - kFadeSubtrahend) / kFadeDenominator), 0, 255);
        dl = DrawLogo(dl, state, intro.titleX, (fadeIn * fadeOut) / 255);

        if (intro.introEyeCounter++ >= kEyeCount1) {
            if (intro.introEyeCounter >= kEyeCount2) {
                intro.introEyeCounter = 0;
                intro.gunbarrelMode += 2;
            }
        }
    }
    return dl;
}

} // namespace

void RegisterRarewareLogo(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                          const std::shared_ptr<FrontState>& front, const std::shared_ptr<IntroState>& intro) {
    GetState().front = front;
    GetState().intro = intro;
    menu.RegisterMenu(MenuId::RarewareLogo,
                      {
                          .onInit = [resourceManager](MenuStateMachine& m) { Init(resourceManager, m); },
                          .onInterface = Interface,
                          .onConstruct = Construct,
                      });
}

} // namespace Frigate::Game::Menu
