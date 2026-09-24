#include "game/menu/EyeIntro.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <spdlog/spdlog.h>

#include "fast/resource/type/Texture.h"
#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/ScreenSize.h"
#include "game/menu/BasicGeometry.h"
#include "game/menu/BloodAnimation.h"
#include "game/menu/IntroState.h"
#include "game/menu/PropCamera.h"
#include "gfx/FixedMtx.h"
#include "gfx/GbiF3dex.h"
#include "gfx/Gu.h"
#include "gfx/TextureCache.h"
#include "model/JointDraw.h"
#include "model/ModelInstance.h"
#include "model/Trig.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/type/Blob.h"

namespace Frigate::Game::Menu {

namespace {

// title.c's NTSC values (PAL and EU differ); this port targets NTSC/USA only
// (see README).
constexpr float kBarrelStep = 6.0f;
constexpr float kTrailGap = 12.0f;
constexpr int32_t kTrailCountdownStep = 6;
constexpr int32_t kTrailCountdownStart = 0x42;
constexpr int32_t kTrailCountdownReset = 200;
constexpr float kSweepEnd = 1390.0f;
constexpr float kSightStart = 1276.0f;
constexpr float kSightStep = 5.8183274f;
constexpr float kSightBondAppears = 600.0f;
constexpr float kSightEnd = -80.0f;
constexpr int kSightHoldFrames = 20;
constexpr int32_t kShakeStep = 0x38E;
constexpr int kShakeFrames = 108;
constexpr int kFadeStep = 8;
constexpr int kFadeEnd = 0xF7;
constexpr int kBlackoutFrames = 0x1e;
constexpr int kBloodFrameTicks = 2;

// initializeGunBarrelIntro()'s starting values.
constexpr float kStartTitleX = -30.0f;
constexpr float kStartTitleY = 482.0f;
constexpr float kStartTransitionX = -100.0f;

// The barrel's ortho space, and where its two parts sit in it.
constexpr float kOrthoWidth = 1280.0f;
constexpr float kOrthoHeight = 960.0f;
constexpr float kSightOffsetX = 768.0f;
constexpr float kSightOffsetY = -40.0f;
constexpr float kSightScaleX = 2.7f;
constexpr float kSightScaleY = 2.57f;
constexpr float kPlaneZ = -5.0f;

// createGunbarrelRenderHole(): the disc's rim is kBarrelSegments steps of a
// half circle, mirrored across its vertical axis.
constexpr int kBarrelSegments = 0x1E;
constexpr float kBarrelRadius = 64.0f;
constexpr int kBarrelVertexCount = kBarrelSegments;

// insert_sniper_sight_eye_intro(): the backdrop's size, and where its rows are
// drawn.
constexpr int kSightWidth = 440;
constexpr int kSightHeight = 299;
constexpr int kSightTop = 0x10;

// insert_bond_eye_intro() and sub_GAME_7F007F30(): NTSC ticks two animation
// steps per frame.
constexpr int kBondTicksPerFrame = 2;
constexpr int kBondFireStart = 137;
constexpr int kBondSpeedUp = 212;
constexpr int kBondShot = 230;
constexpr int32_t kBondShadowLevel = 80; // sub_GAME_7F073FC8(80)
constexpr float kBondScale = 0.18779343f;
constexpr float kBondFovY = 46.0f;
constexpr float kBondAspect = 320.0f / 240.0f;
constexpr float kBondNear = 10.0f;
constexpr float kBondFar = 10000.0f;
constexpr std::array<float, 3> kBondEye = { 1758.2957f, 220.0f, 684.28143f };
constexpr std::array<float, 3> kBondLook = { -0.97f, 0.0f, 0.24f };

constexpr const char* kSightImage = "eyeintro/sight";
constexpr const char* kBloodStream = "eyeintro/blood";

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<IntroState> intro;
    std::shared_ptr<Ship::ResourceManager> resourceManager;

    // Read by address while the interpreter draws the frame.
    Vp_t viewport{};
    Mtx::FixedMtx ortho{};       // matrixBufferGunbarrel0
    Mtx::FixedMtx logoPlane{};   // matrixBufferRareLogo2
    Mtx::FixedMtx barrelPlane{}; // matrixBufferGunbarrel1
    Mtx::FixedMtx bondView{};    // matrixBufferIntroBackdrop
    Mtx::FixedMtx bondProjection{};
    std::array<Vtx_t, kBarrelVertexCount> barrel{};

    std::shared_ptr<Fast::Texture> sight;
    std::optional<BloodAnimation> blood;
    bool bloodDone = false;

    std::shared_ptr<Model::ModelInstance> chr;
    std::shared_ptr<Model::ModelInstance> gun;
    const AnimationClip* fireClip = nullptr;
    int bondTimer = 0;
    std::optional<Model::JointDraw> bondDraw;
    Mat4 bondBase{};
};

State& GetState() {
    static State state;
    return state;
}

uint8_t Byte(float value) {
    return static_cast<uint8_t>(static_cast<int>(value));
}

// The screen shakes by a sine of a phase that steps every frame, about the
// position the blood left it at.
void Shake(IntroState& intro) {
    intro.barrelPhase += kShakeStep;
    intro.titleX = ((Sins(static_cast<uint16_t>(intro.barrelPhase)) * 64.0f) / 32768.0f) + intro.titleTransitionX;
}

// createGunbarrelRenderHole(): the barrel's disc as vertices going down its
// rim, each step's left and right points together.
void BuildBarrel(std::array<Vtx_t, kBarrelVertexCount>& vertices) {
    size_t next = 0;
    auto put = [&](int16_t x, int16_t y, uint8_t shade) {
        Vtx_t& vertex = vertices[next++];
        vertex.ob[0] = x;
        vertex.ob[1] = y;
        vertex.ob[2] = 0;
        vertex.flag = 0;
        vertex.tc[0] = 0;
        vertex.tc[1] = 0;
        vertex.cn[0] = vertex.cn[1] = vertex.cn[2] = shade;
        vertex.cn[3] = 0;
    };
    for (int i = 0; i <= kBarrelSegments; i += 2) {
        const float angle = (static_cast<float>(i) * 3.1415927f) / static_cast<float>(kBarrelSegments);
        const auto x = static_cast<int16_t>(std::sin(angle) * kBarrelRadius);
        const auto y = static_cast<int16_t>(std::cos(angle) * -kBarrelRadius);
        const uint8_t shade = Byte(143.0f - (std::cos(angle) * -111.0f));
        put(x, y, shade);
        if (i != 0 && i < kBarrelSegments) {
            put(static_cast<int16_t>(-x), y, shade);
        }
    }
}

// sub_GAME_7F01BFF8(): triangle strips over the barrel's vertices, loaded 16 at
// a time with 14 of overlap.
Gfx* AppendBarrel(Gfx* dl, const State& state) {
    using namespace Frigate::F3dex;
    int32_t remaining = kBarrelVertexCount;
    size_t first = 0;
    do {
        int32_t count = remaining >= 0x11 ? 0x10 : remaining;
        *dl++ = Vertices(&state.barrel[first], static_cast<uint32_t>(count));
        first += 14;
        for (int32_t j = count - 3; j >= 0; j--) {
            *dl++ = Triangle(j, j + 1, j + 2);
        }
        remaining -= 14;
    } while (remaining >= 3);
    return dl;
}

Gfx* SetRenderMode(Gfx* dl, uint32_t mode1, uint32_t mode2) {
    *dl++ = F3dex::SetOtherModeL(G_MDSFT_RENDERMODE, 29, mode1 | mode2);
    return dl;
}

Gfx* SetCycle1(Gfx* dl) {
    *dl++ = F3dex::SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE);
    return dl;
}

// sub_GAME_7F01C1A4(): the barrel's ortho projection and the logo-plane
// modelview, and the 1-cycle opaque state its callers draw in. The modelview
// loads first: the interpreter multiplies a projection load by the top of
// the modelview stack, which is empty until the first modelview load.
Gfx* SetupOrtho(Gfx* dl, State& state) {
    using namespace Frigate::F3dex;
    *dl++ = Matrix(&state.logoPlane, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
    *dl++ = Matrix(&state.ortho, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
    gDPPipeSync(dl++);
    dl = SetCycle1(dl);
    dl = SetRenderMode(dl, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
    *dl++ = SetGeometryMode(G_SHADE | Fast::F3DEX_G_SHADING_SMOOTH);
    return dl;
}

void SetPlanes(State& state, float logoX, float logoY, float barrelX, float barrelY) {
    Gu::MatrixF matrix;
    Gu::TranslateF(matrix, logoX, logoY, kPlaneZ);
    state.logoPlane = Mtx::ToFixedMtx(matrix);
    Gu::TranslateF(matrix, barrelX, barrelY, kPlaneZ);
    state.barrelPlane = Mtx::ToFixedMtx(matrix);
}

// manipulateGunbarrelAndLogoMatrices(): the barrel's two discs, the leading
// one at (g_TitleX, g_TitleY) and the trailing at titleTransition.
Gfx* DrawSweep(Gfx* dl, State& state) {
    using namespace Frigate::F3dex;
    const IntroState& intro = *state.intro;
    SetPlanes(state, intro.titleX, intro.titleY, intro.titleTransitionX, intro.titleTransitionY);
    dl = BasicGeometry(dl);
    dl = SetupOrtho(dl, state);

    gDPSetCombineMode(dl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(dl++, 0, 0, 0xE6, 0xE6, 0xE6, 0x00);
    dl = AppendBarrel(dl, state);
    *dl++ = Matrix(&state.barrelPlane, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
    return AppendBarrel(dl, state);
}

// gDPLoadTextureBlock() of one row of the 8-bit backdrop, clamped in both
// axes. The SETTIMG carries the image's real row width (see
// src/game/TextRender.cpp).
Gfx* LoadSightRow(Gfx* dl, const uint8_t* row) {
    gDPSetTextureImage(dl++, G_IM_FMT_I, G_IM_SIZ_8b, kSightWidth, const_cast<uint8_t*>(row));
    gDPSetTile(dl++, G_IM_FMT_I, G_IM_SIZ_8b, 0, 0, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
               G_TX_NOLOD, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOLOD);
    gDPLoadSync(dl++);
    constexpr int kLine = (kSightWidth + 7) / 8;
    gDPLoadBlock(dl++, G_TX_LOADTILE, 0, 0, kSightWidth - 1, (2048 + kLine - 1) / kLine);
    gDPPipeSync(dl++);
    gDPSetTile(dl++, G_IM_FMT_I, G_IM_SIZ_8b, kLine, 0, G_TX_RENDERTILE, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
               G_TX_NOLOD, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOLOD);
    gDPSetTileSize(dl++, G_TX_RENDERTILE, 0, 0, (kSightWidth - 1) << G_TEXTURE_IMAGE_FRAC, 0);
    return dl;
}

// insert_sniper_sight_eye_intro() and titleRenderFolderMenuBackgroundLines():
// the sight's backdrop, one row at a time, tinted from black at the top to
// white at the bottom and scrolled by g_TitleX.
Gfx* DrawSightBackground(Gfx* dl, State& state) {
    using namespace Frigate::F3dex;
    dl = BasicGeometry(dl);
    gDPSetCombineMode(dl++, G_CC_MODULATEI_PRIM, G_CC_MODULATEI_PRIM);

    dl = SetRenderMode(dl, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    dl = SetCycle1(dl);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_NONE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_POINT);
    gDPPipeSync(dl++);

    const auto xOffset = static_cast<int32_t>(std::floor((kMenuScreenWidth * state.intro->titleX) / kOrthoWidth));
    const uint8_t* image = state.sight->ImageData;
    for (int i = 0; i < kSightHeight; i++) {
        dl = LoadSightRow(dl, image + i * kSightWidth);
        const uint8_t level = Byte((255.0f * static_cast<float>(i)) / static_cast<float>(kSightHeight));
        gDPSetPrimColor(dl++, 0, 0, level, level, level, 255);

        // The decomp ends each rectangle a quarter pixel short of the next
        // row's top, which the RDP rasterises as the same one pixel row. The
        // interpreter draws the fraction, leaving seams once the frame is
        // scaled, so the rectangles end on the pixel edge.
        const int32_t top = (i + kSightTop) << 2;
        const int32_t bottom = (i + 1 + kSightTop) << 2;
        if (xOffset < 0) {
            gSPTextureRectangle(dl++, 0, top, kSightWidth << 2, bottom, G_TX_RENDERTILE, (-xOffset) << 5, 0, 1 << 10,
                                1 << 10);
        } else {
            gSPTextureRectangle(dl++, xOffset << 2, top, kSightWidth << 2, bottom, G_TX_RENDERTILE, 0, 0, 1 << 10,
                                1 << 10);
        }
    }
    return dl;
}

// insert_sight_backdrop_eye_intro(): the disc the sight looks through,
// stretched and shaded by vertex colour, over the scrolling background.
Gfx* DrawSightDisc(Gfx* dl, State& state) {
    using namespace Frigate::F3dex;
    const IntroState& intro = *state.intro;
    Gu::MatrixF matrix;
    Gu::TranslateF(matrix, intro.titleX + kSightOffsetX, intro.titleY + kSightOffsetY, kPlaneZ);
    state.logoPlane = Mtx::ToFixedMtx(matrix);
    Gu::ScaleF(matrix, kSightScaleX, kSightScaleY, 1.0f);
    state.barrelPlane = Mtx::ToFixedMtx(matrix);

    dl = BasicGeometry(dl);
    // gSPDisplayList(dlFastPipelineSetup)
    dl = SetCycle1(dl);
    *dl++ = SetOtherModeH(G_MDSFT_PIPELINE, 1, G_PM_1PRIMITIVE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_TILE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTDETAIL, 2, G_TD_CLAMP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_PERSP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_BILERP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTCONV, 3, G_TC_FILT);
    gDPSetCombineMode(dl++, G_CC_SHADE, G_CC_SHADE);
    *dl++ = SetOtherModeH(G_MDSFT_COMBKEY, 1, G_CK_NONE);
    *dl++ = SetOtherModeL(G_MDSFT_ALPHACOMPARE, 2, G_AC_NONE);
    dl = SetRenderMode(dl, G_RM_PASS, G_RM_OPA_SURF2);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_MAGICSQ);
    gDPPipeSync(dl++);

    dl = SetupOrtho(dl, state);
    *dl++ = Matrix(&state.barrelPlane, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_MUL);
    return AppendBarrel(dl, state);
}

// The sight's alpha-blended full-screen fills: sub_GAME_7F01CA18()'s red
// flash (alpha 180) and sub_GAME_7F007E70()'s fade to black.
Gfx* DrawFill(Gfx* dl, State& state, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
    using namespace Frigate::F3dex;
    dl = SetupOrtho(dl, state);
    dl = SetRenderMode(dl, G_RM_CLD_SURF, G_RM_CLD_SURF2);
    gDPSetCombineMode(dl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(dl++, 0, 0, r, g, b, alpha);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_MAGICSQ);
    gDPFillRectangle(dl++, 0, 0, kMenuScreenWidth, kMenuScreenHeight);
    return dl;
}

// gunbarrelBloodOverlayDL(): the current blood frame, a 96x80 4-bit
// intensity texture stretched over the screen and tinted red.
Gfx* DrawBlood(Gfx* dl, State& state) {
    using namespace Frigate::F3dex;
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_BILERP);
    dl = SetupOrtho(dl, state);
    *dl++ = Texture(0x8000, 0x8000, 0, G_TX_RENDERTILE, 1);
    dl = SetRenderMode(dl, G_RM_CLD_SURF, G_RM_CLD_SURF2);
    gDPSetCombineMode(dl++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_MAGICSQ);
    gDPSetPrimColor(dl++, 0, 0, 0x96, 0x00, 0x00, 0xB4);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_NONE);

    // gDPLoadTextureBlock_4b(): loaded as 16-bit texels, drawn as 4-bit. The
    // SETTIMG's width is the real row width in 16-bit texels.
    constexpr int kRowWords = kBloodTextureWidth / 4;
    gDPSetTextureImage(dl++, G_IM_FMT_I, G_IM_SIZ_16b, kRowWords, const_cast<uint8_t*>(state.blood->Texture()));
    gDPSetTile(dl++, G_IM_FMT_I, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
               G_TX_NOLOD, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOLOD);
    gDPLoadSync(dl++);
    constexpr int kLine = (kRowWords + 3) / 4;
    gDPLoadBlock(dl++, G_TX_LOADTILE, 0, 0, ((kBloodTextureWidth * kBloodTextureHeight + 3) >> 2) - 1,
                 (2048 + kLine - 1) / kLine);
    gDPPipeSync(dl++);
    gDPSetTile(dl++, G_IM_FMT_I, G_IM_SIZ_4b, ((kBloodTextureWidth >> 1) + 7) >> 3, 0, G_TX_RENDERTILE, 0,
               G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
               G_TX_NOLOD);
    gDPSetTileSize(dl++, G_TX_RENDERTILE, 0, 0, (kBloodTextureWidth - 1) << G_TEXTURE_IMAGE_FRAC,
                   (kBloodTextureHeight - 1) << G_TEXTURE_IMAGE_FRAC);
    gSPTextureRectangle(dl++, 0, 0, (kMenuScreenWidth * 4) - 1, (kMenuScreenHeight * 4) - 1, G_TX_RENDERTILE, 0, 0,
                        0x18000 / kMenuScreenWidth, 0x14000 / kMenuScreenHeight);
    return dl;
}

// sub_GAME_7F007F30(): steps Bond's animation and poses him and his gun.
void PoseBond(State& state) {
    bool shot = false;
    for (int i = 0; i < kBondTicksPerFrame; i++) {
        if (state.bondTimer >= 0) {
            state.bondTimer++;
            if (state.bondTimer == kBondFireStart) {
                state.chr->playback.SetAnimation(state.fireClip, false, 2.0f, 0.91f, 16.0f);
            }
            if (state.bondTimer == kBondSpeedUp) {
                state.chr->playback.SetAnimSpeed(1.6f, 8.0f);
            }
        }
        state.chr->playback.TickAnim(1, true);
        if (state.bondTimer == kBondShot) {
            // TODO(https://github.com/liz-baker/frigate/issues/41): sndPlaySfx(GUN_RIFLE7BIG_1_SFX).
            shot = true;
        }
    }

    state.chr->playback.CalcPos();

    // The gun's gunfire node (switch slot 0) shows the muzzle flash on the
    // shot frame, and its slot-2 switch is shown on the same frame.
    state.gun->SetGunfireVisible(0, shot);
    state.gun->SetSwitchVisible(2, shot);

    state.chr->Pose(state.bondBase);
    state.gun->Pose(state.bondBase);
}

// insert_bond_eye_intro(): Bond in the sight, drawn through a perspective
// camera.
Gfx* DrawBond(Gfx* dl, State& state) {
    using namespace Frigate::F3dex;
    if (state.chr == nullptr || state.gun == nullptr) {
        return dl;
    }
    PoseBond(state);

    ModelRenderParams params;
    params.zbufferEnabled = false;
    params.propType = kRenderPropViewer;
    params.shadowLevel = kBondShadowLevel;
    state.bondDraw = Model::BuildJointDraw(state.resourceManager, { state.chr, state.gun }, params);
    if (!state.bondDraw) {
        return dl;
    }

    gDPSetCombineMode(dl++, G_CC_SHADE, G_CC_SHADE);
    dl = SetRenderMode(dl, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
    *dl++ = Matrix(&state.bondView, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
    *dl++ = Matrix(&state.bondProjection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
    for (const Gfx& command : state.bondDraw->commands) {
        *dl++ = command;
    }
    return dl;
}

Gfx* DrawSight(Gfx* dl, State& state) {
    dl = DrawSightBackground(dl, state);
    return DrawSightDisc(dl, state);
}

void SetupBond(State& state) {
    const auto& resourceManager = state.resourceManager;
    state.chr = Model::ModelInstance::Create(resourceManager, "models/chr/CdjbondZ");
    state.gun = Model::ModelInstance::Create(resourceManager, "models/prop/PchrwppkZ");
    if (state.chr == nullptr || state.gun == nullptr) {
        state.chr.reset();
        state.gun.reset();
        return;
    }
    state.chr->AttachHead(resourceManager, "models/chr/CheadbrosnanZ", /*sunglasses=*/false);

    // modelSetScale, modelSetAnimTranslationScale, setsuboffset, setsubroty
    // and modelSetAnimPlaySpeed.
    ModelPlayback& playback = state.chr->playback;
    playback.scale = kBondScale;
    playback.animTranslationScale = 1.0f;
    playback.SetPosition({ 0.0f, 0.0f, 0.0f });
    playback.SetHeading(0.0f);
    playback.SetAnimPlaySpeed(0.5f, 0.0f);

    const AnimationClip* walk = state.chr->LoadClip(resourceManager, "bond_eye_walk");
    state.fireClip = state.chr->LoadClip(resourceManager, "bond_eye_fire");
    if (walk == nullptr || state.fireClip == nullptr) {
        spdlog::error("eye intro: Bond's animations are missing from the loaded archives");
        state.chr.reset();
        state.gun.reset();
        return;
    }
    // Starts 0x44 frames before the walk's end, wrapping.
    auto startFrame = static_cast<int>(walk->frameCount) - 0x44;
    while (startFrame < 0) {
        startFrame += static_cast<int>(walk->frameCount);
    }
    playback.SetAnimation(walk, false, static_cast<float>(startFrame), 0.91f, 0.0f);

    state.gun->playback.scale = kBondScale;
    state.gun->AttachTo(state.chr, 3);

    // modelSetDistanceDisabled(1) around the draw.
    state.chr->lod.distanceDisabled = true;
    state.gun->lod.distanceDisabled = true;
    state.bondTimer = 0;

    Gu::MatrixF matrix;
    Gu::LookAtF(matrix, kBondEye[0], kBondEye[1], kBondEye[2], kBondEye[0] + kBondLook[0], kBondEye[1] + kBondLook[1],
                kBondEye[2] + kBondLook[2], 0.0f, 1.0f, 0.0f);
    for (int row = 0; row < 4; row++) {
        for (int column = 0; column < 4; column++) {
            state.bondBase.m[row][column] = matrix[row][column];
        }
    }
    Gu::TranslateF(matrix, 0.0f, 0.0f, 0.0f);
    state.bondView = Mtx::ToFixedMtx(matrix);
    Gu::PerspectiveF(matrix, kBondFovY, kBondAspect, kBondNear, kBondFar, 1.0f);
    state.bondProjection = Mtx::ToFixedMtx(matrix);
}

void Init(const std::shared_ptr<Ship::ResourceManager>& resourceManager, MenuStateMachine&) {
    State& state = GetState();
    IntroState& intro = *state.intro;

    // initializeGunBarrelIntro(). The decomp also builds the barrel and
    // sight in a scratch buffer and loads the models into RAM pools; here
    // the barrel is a vertex array, the sight and blood stream are packed
    // resources, and the models load through ModelInstance.
    intro.gunbarrelMode = 2;
    intro.titleX = kStartTitleX;
    intro.titleY = kStartTitleY;
    intro.titleTransitionX = kStartTransitionX;
    intro.titleTransitionY = kStartTitleY;
    intro.barrelPhase = kTrailCountdownStart;
    intro.introEyeCounter = 0;

    // TODO(https://github.com/liz-baker/frigate/issues/41): musicTrack1Play(M_INTRO).

    state.resourceManager = resourceManager;
    state.viewport = MenuViewport();

    Gu::MatrixF ortho;
    Gu::OrthoF(ortho, 0.0f, kOrthoWidth, 0.0f, kOrthoHeight, 1.0f, 8.0f, 256.0f);
    state.ortho = Mtx::ToFixedMtx(ortho);
    BuildBarrel(state.barrel);

    state.sight = resourceManager->LoadResource<Fast::Texture>(kSightImage);
    auto stream = resourceManager->LoadResource<Ship::Blob>(kBloodStream);
    if (state.sight == nullptr || stream == nullptr) {
        spdlog::error("eye intro: the sight image or blood stream is missing from the loaded archives");
        state.blood.reset();
    } else {
        state.blood.emplace(stream->Data);
    }
    state.bloodDone = false;

    SetupBond(state);
}

void Update() {
    State& state = GetState();
    // clearChrGunModelInstances()
    state.chr.reset();
    state.gun.reset();
    state.fireClip = nullptr;
    state.bondDraw.reset();
    state.blood.reset();
    state.sight.reset();
}

void Interface(MenuStateMachine& menu) {
    FrontState& front = *GetState().front;
    if (GetState().intro->gunbarrelMode == 9) {
        menu.ChangeMenu(MenuId::GoldenEyeLogo, /*reload=*/true);
        return;
    }
    if (Input::AnyButtonPressedThisFrame()) {
        if (!front.isFirstTimeOnMainMenu) {
            menu.ChangeMenu(MenuId::FileSelect, /*reload=*/true);
            return;
        }
        front.prevKeypresses = true;
        menu.ChangeMenu(MenuId::GoldenEyeLogo, /*reload=*/true);
    }
}

// renderGunbarrelEyeIntroSequence(). The framebuffer clear each of its draws
// begins with is the one the game loop already emits ahead of every
// constructor.
Gfx* Construct(Gfx* dl, MenuStateMachine& menu) {
    State& state = GetState();
    IntroState& intro = *state.intro;
    if (state.sight == nullptr || !state.blood) {
        return dl;
    }

    // On hardware the sweep and the sight up to Bond entering it run a frame a
    // tick, and every frame with Bond in it a frame every two (see
    // MenuStateMachine::ClockTimer).
    const bool bondDrawn = intro.gunbarrelMode >= 4 || (intro.gunbarrelMode == 3 && intro.titleX < kSightBondAppears);
    menu.SetClockTimer(bondDrawn ? 2 : 1);

    // The viewport comes from the game's VI setup in the decomp, which this
    // port doesn't have.
    *dl++ = F3dex::Viewport(&state.viewport);

    switch (intro.gunbarrelMode) {
        case 2:
            dl = DrawSweep(dl, state);
            intro.titleX += kBarrelStep;
            if (intro.barrelPhase < 0) {
                intro.barrelPhase = kTrailCountdownReset;
                intro.titleTransitionX = intro.titleX - kTrailGap;
            } else {
                intro.barrelPhase -= kTrailCountdownStep;
            }
            if (intro.titleX > kSweepEnd) {
                intro.gunbarrelMode++;
                intro.titleX = kSightStart;
            }
            break;

        case 3:
            dl = DrawSight(dl, state);
            if (intro.titleX < kSightBondAppears) {
                dl = DrawBond(dl, state);
            }
            intro.titleX -= kSightStep;
            if (intro.titleX <= kSightEnd) {
                intro.gunbarrelMode++;
                intro.introEyeCounter = kSightHoldFrames;
            }
            break;

        case 4:
            dl = DrawSight(dl, state);
            dl = DrawBond(dl, state);
            intro.introEyeCounter--;
            if (intro.introEyeCounter < 0) {
                intro.gunbarrelMode++;
                state.blood->Start();
                TextureCache::Invalidate(state.blood->Texture());
                state.bloodDone = false;
                intro.introEyeCounter = 1;
            }
            break;

        case 5:
            intro.introEyeCounter--;
            if (intro.introEyeCounter == 0) {
                state.bloodDone = state.blood->Advance();
                TextureCache::Invalidate(state.blood->Texture());
                intro.introEyeCounter = kBloodFrameTicks;
            }
            dl = DrawSight(dl, state);
            dl = DrawBond(dl, state);
            dl = DrawBlood(dl, state);
            if (state.bloodDone) {
                intro.gunbarrelMode++;
                intro.barrelPhase = 0;
                intro.titleTransitionX = intro.titleX;
                intro.introEyeCounter = 0;
            }
            break;

        case 6:
            intro.introEyeCounter++;
            Shake(intro);
            dl = DrawSight(dl, state);
            dl = DrawBond(dl, state);
            dl = DrawFill(dl, state, 150, 0, 0, 180);
            if (intro.introEyeCounter >= kShakeFrames) {
                intro.introEyeCounter = 0;
                intro.gunbarrelMode++;
            }
            break;

        case 7:
            Shake(intro);
            dl = DrawSight(dl, state);
            dl = DrawBond(dl, state);
            dl = DrawFill(dl, state, 150, 0, 0, 180);
            intro.introEyeCounter += kFadeStep;
            dl = DrawFill(dl, state, 0, 0, 0, static_cast<uint8_t>(intro.introEyeCounter));
            if (intro.introEyeCounter >= kFadeEnd) {
                intro.introEyeCounter = 0;
                intro.gunbarrelMode++;
            }
            break;

        case 8:
            dl = BasicGeometry(dl);
            if (intro.introEyeCounter++ >= kBlackoutFrames) {
                intro.introEyeCounter = 0;
                intro.gunbarrelMode++;
            }
            break;

        default:
            break;
    }
    return dl;
}

} // namespace

void RegisterEyeIntro(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                      const std::shared_ptr<FrontState>& front, const std::shared_ptr<IntroState>& intro) {
    GetState().front = front;
    GetState().intro = intro;
    menu.RegisterMenu(MenuId::EyeIntro,
                      {
                          .onUpdate = Update,
                          .onInit = [resourceManager](MenuStateMachine& m) { Init(resourceManager, m); },
                          .onInterface = Interface,
                          .onConstruct = Construct,
                      });
}

} // namespace Frigate::Game::Menu
