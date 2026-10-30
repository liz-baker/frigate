#include "game/menu/CastScreen.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "game/ChrRecords.h"
#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/Sound.h"
#include "game/menu/RunStage.h"
#include "game/ScreenSize.h"
#include "game/save/SaveProgress.h"
#include "game/TextRender.h"
#include "game/menu/BasicGeometry.h"
#include "game/menu/CastData.h"
#include "game/menu/MissionSelectModel.h"
#include "game/menu/PropCamera.h"
#include "gfx/FixedMtx.h"
#include "gfx/GbiF3dex.h"
#include "gfx/Gu.h"
#include "model/JointDraw.h"
#include "model/ModelInstance.h"
#include "model/Random.h"
#include "resource/type/Font.h"
#include "resource/type/Text.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

// The cast draws at a 46 degree field of view, unlike every other front-end
// screen's 60.
constexpr float kFovY = 46.0f;
constexpr float kAspect = 1.3333334f; // ASPECT_RATIO_SD
constexpr float kNear = 10.0f;
constexpr float kFar = 2000.0f;

// Placeholder addresses for the color and depth images; the interpreter
// recognises a fill whose color image is the depth image as a depth clear.
constexpr uintptr_t kFramebufferAddress = 0x00100000;
constexpr uintptr_t kZBufferAddress = 0x00200000;

// The completions the weapon substitutions and unlockable characters read,
// from the player's folders; with no save loaded nothing is completed.
Cast::Unlocks CurrentUnlocks(const FrontState& front) {
    const Save::SaveProgress* progress = front.progress.get();
    if (progress == nullptr) {
        return {};
    }
    return Cast::QueryUnlocks({
        .aztecOnSecretAgentOr00 = [progress] { return progress->IsAztecCompletedOnSecretOr00AnyFolder(); },
        .egyptOn00 = [progress] { return progress->IsEgyptCompletedOn00AnyFolder(); },
        .cradle = [progress] { return progress->IsCradleCompletedAnyFolder(); },
    });
}

constexpr float kModelScale = 0.1f;
constexpr float kAnimPlaySpeed = 0.5f;
constexpr float kPi = 3.14159265f;
// The weapon's holder node in the character's switch array: the right hand,
// or the left when the animation is mirrored.
constexpr size_t kRightHandSlot = 3;
constexpr size_t kLeftHandSlot = 5;
// The weapon's switches that are hidden: its muzzle flash (a gunfire node in
// slot 0) and the switch in slot 2.
constexpr size_t kFlashSlot = 0;
constexpr size_t kHiddenSwitchSlot = 2;

// g_GlobalTimerDelta isn't advanced in the front-end, and stays at the 1.0
// the level start sets.
constexpr float kTimerDelta = 1.0f;

// Where the text lines sit: centred on x, top at y.
constexpr int32_t kTextCentreX = 315;
constexpr int32_t kTextTop[3] = { 108, 152, 174 };

constexpr const char* kFontPath = "font/fontZurichBold";
constexpr const char* kStringsPath = "obseg/text/LtitleE";

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    std::shared_ptr<ChrRecords> records;
    std::shared_ptr<Font> font;
    std::shared_ptr<Frigate::Text> strings;

    // Read by address while the interpreter draws the frame.
    Vp_t viewport{};
    Mtx::FixedMtx view{};
    Mtx::FixedMtx projection{};
    Lights1 lights = gdSPDefLights1(0x96, 0x96, 0x96, 0xFF, 0xFF, 0xFF, 77, 77, 46); // gelogolight
    LookAt lookAt{};

    std::shared_ptr<Model::ModelInstance> chr;
    std::shared_ptr<Model::ModelInstance> gun;
    std::optional<Model::JointDraw> draw;
    Cast::CameraPath path;
    Cast::RootFollow follow;
};

State& GetState() {
    static State state;
    return state;
}

// Each line is drawn on a fully transparent rectangle first, as the decomp
// does, then at `fade` of full white.
Gfx* DrawLine(Gfx* dl, const State& state, int slot, int32_t top, float fade) {
    static const std::string kEmpty;
    const std::string& text =
        static_cast<size_t>(slot) < state.strings->Strings.size() ? state.strings->Strings[slot] : kEmpty;
    const Text::Extent extent = Text::Measure(*state.font, text);
    int32_t x = kTextCentreX - extent.width / 2;
    int32_t y = top;
    dl = Text::FillRect(dl, x, top, x + extent.width + 1, top + extent.height + 1, 0);
    const uint32_t colour = static_cast<uint32_t>(255.0f * fade) | 0xFFFFFF00u;
    return Text::Render(dl, &x, &y, text, *state.font, colour, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
}

// The screen-wide black fill whose alpha is what is left of `fade`.
Gfx* DrawFade(Gfx* dl, float fade) {
    using namespace Frigate::F3dex;
    gDPSetScissor(dl++, G_SC_NON_INTERLACE, 0, 0, kMenuScreenWidth, kMenuScreenHeight);
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_DISABLE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_NONE);
    *dl++ = SetOtherModeL(G_MDSFT_ALPHACOMPARE, 2, G_AC_NONE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_TILE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_BILERP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTCONV, 3, G_TC_FILT);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_CLD_SURF | G_RM_CLD_SURF2);
    gDPSetCombineMode(dl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(dl++, 0, 0, 0, 0, 0, static_cast<uint8_t>(0xFF - static_cast<int32_t>(255.0f * fade)));
    gDPFillRectangle(dl++, 0, 0, kMenuScreenWidth, kMenuScreenHeight);
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_BAYER);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_PERSP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_LOD);
    return dl;
}

Mat4 ToMat4(const Gu::MatrixF& matrix) {
    Mat4 out;
    for (int row = 0; row < 4; row++) {
        for (int column = 0; column < 4; column++) {
            out.m[row][column] = matrix[row][column];
        }
    }
    return out;
}

std::shared_ptr<Model::ModelInstance> LoadCharacter(State& state, const Cast::BodyAndHead& chosen) {
    const ChrRecord* body = state.records->Find(chosen.body);
    if (body == nullptr) {
        spdlog::error("cast: no body '{}' in the chr records", chosen.body);
        return nullptr;
    }
    auto chr = Model::ModelInstance::Create(state.resourceManager, body->modelPath);
    if (chr == nullptr) {
        return nullptr;
    }
    // A body whose model carries its own head takes no other.
    if (!chosen.head.empty() && !body->hasHead) {
        const ChrRecord* head = state.records->Find(chosen.head);
        if (head == nullptr || !chr->AttachHead(state.resourceManager, head->modelPath, /*sunglasses=*/false)) {
            spdlog::error("cast: can't attach head '{}' to '{}'", chosen.head, chosen.body);
            return nullptr;
        }
    }
    return chr;
}

// init_menu18_displaycast(). The order the random numbers are drawn in is the
// decomp's.
void Init(const std::shared_ptr<Ship::ResourceManager>& resourceManager, MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;
    menu.ResetMenuTimer();

    state.resourceManager = resourceManager;
    state.chr.reset();
    state.gun.reset();
    state.draw.reset();

    if (state.records == nullptr) {
        state.records = ChrRecords::Load(resourceManager);
    }
    state.font = resourceManager->LoadResource<Font>(kFontPath);
    state.strings = resourceManager->LoadResource<Frigate::Text>(kStringsPath);
    if (state.records == nullptr || state.font == nullptr || state.strings == nullptr) {
        spdlog::error("cast: the character records, font or text are missing from the loaded archives");
        return;
    }

    const Cast::Random random = [] { return RandomGetNext(); };
    const bool flip = (random() & 1) != 0;

    if (front.fullActorIntro && front.introCharacterIndex == 0) {
        Sound::MusicTrackApplySeqpVol(0, AudioDriver::kMusicVolumeMax);
        Sound::SetMusicTrackFade(0, AudioDriver::kMusicFadeUnset);
        Sound::MusicTrackPlay(0, AudioDriver::kMusicIntro);
    }

    const auto animations = Cast::Animations();
    const Cast::Animation& animation = animations[random() % animations.size()];
    const Cast::Member& member = Cast::Members()[static_cast<size_t>(front.introCharacterIndex)];
    const Cast::BodyAndHead chosen = Cast::ChooseBodyAndHead(
        member,
        [&state](std::string_view body) {
            const ChrRecord* record = state.records->Find(body);
            return record != nullptr && record->isMale;
        },
        random);

    state.chr = LoadCharacter(state, chosen);
    if (state.chr == nullptr) {
        return;
    }

    const std::string_view weapon = Cast::ChooseWeapon(animation.weapon, CurrentUnlocks(front), random);
    if (!weapon.empty()) {
        state.gun = Model::ModelInstance::Create(state.resourceManager, std::string(weapon));
        if (state.gun != nullptr) {
            state.gun->playback.scale = kModelScale;
            state.gun->AttachTo(state.chr, flip ? kLeftHandSlot : kRightHandSlot, flip ? kPi : 0.0f);
            state.gun->lod.distanceDisabled = true;
        }
    }

    // modelSetScale, modelSetAnimTranslationScale, setsuboffset, setsubroty
    // and modelSetAnimPlaySpeed. The scales replace the ones the body's
    // record gives.
    ModelPlayback& playback = state.chr->playback;
    playback.scale = kModelScale;
    playback.animTranslationScale = kModelScale;
    playback.SetPosition({ 0.0f, 0.0f, 0.0f });
    playback.SetHeading(0.0f);
    playback.SetAnimPlaySpeed(kAnimPlaySpeed, 0.0f);
    state.chr->lod.distanceDisabled = true;

    const AnimationClip* clip = state.chr->LoadClip(resourceManager, std::string(animation.clip));
    if (clip == nullptr) {
        state.chr.reset();
        state.gun.reset();
        return;
    }
    playback.SetAnimation(clip, flip, animation.startFrame, animation.speed, 0.0f);

    state.path = Cast::ChooseCameraPath(random);
    state.follow = Cast::RootFollow{};

    state.viewport = MenuViewport();
    Gu::MatrixF matrix;
    Gu::TranslateF(matrix, 0.0f, 0.0f, 0.0f);
    state.view = Mtx::ToFixedMtx(matrix);
    Gu::PerspectiveF(matrix, kFovY, kAspect, kNear, kFar, 1.0f);
    state.projection = Mtx::ToFixedMtx(matrix);
    Gu::LookAtReflect(state.lookAt, 0.0f, 0.0f, 4000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
}

// update_menu18_displaycast(): the models are let go as the screen is left.
void Update(const MenuStateMachine&) {
    State& state = GetState();
    state.chr.reset();
    state.gun.reset();
    state.draw.reset();
    state.font.reset();
    state.strings.reset();
}

// interface_menu18_displaycast().
void Interface(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;
    menu.AddMenuTimer(menu.ClockTimer());

    if (menu.MenuTimer() >= Cast::kEndTimer) {
        const Cast::Random random = [] { return RandomGetNext(); };
        auto next = Cast::NextMember(static_cast<size_t>(front.introCharacterIndex), front.fullActorIntro,
                                     CurrentUnlocks(front), random);
        if (next >= Cast::Members().size()) {
            next = 0;
        }
        front.introCharacterIndex = static_cast<int>(next);

        if (next > 0) {
            menu.SwitchMenu(MenuId::DisplayCast, /*reload=*/true);
        } else if (front.fullActorIntro) {
            // set_cursor_to_stage_solo(SP_LEVEL_CRADLE).
            menu.SwitchMenu(MenuId::MissionSelect, /*reload=*/true);
            MissionSelectModel::PlaceCursorOnStage(Save::kLevelCradle, front.cursor);
            front.fullActorIntro = false;
        } else {
            if (!SelectRamromToPlay(menu, front, state.resourceManager)) {
                menu.SwitchMenu(MenuId::FileSelect, /*reload=*/true);
            }
        }
    } else if (Input::AnyButtonPressedThisFrame() && !front.fullActorIntro) {
        menu.SwitchMenu(MenuId::FileSelect, /*reload=*/true);
    }
}

// Poses the character and, if it has one, the weapon in its hand, for the
// camera the character's own movement steers.
void PoseModels(State& state, const MenuStateMachine& menu, Cast::Orbit& orbit) {
    ModelPlayback& playback = state.chr->playback;
    // modelTickAnim(), then subcalcpos().
    playback.TickAnim(menu.ClockTimer(), true);
    playback.CalcPos();

    if (state.gun != nullptr) {
        state.gun->SetGunfireVisible(kFlashSlot, false);
        state.gun->SetSwitchVisible(kHiddenSwitchSlot, false);
    }

    // The first pose, against no camera, is where the character stands. The
    // camera's target follows matrix 0 of it (render_pos[0] in the decomp),
    // which for a character is the head joint's, not the root's.
    state.chr->Pose(IdentityMat4());
    const Cast::FollowInput input{ { playback.root.pos.x, playback.root.pos.y, playback.root.pos.z },
                                   state.chr->Walk().matrices[0],
                                   menu.ClockTimer(),
                                   kTimerDelta };
    Cast::Step(state.follow, input, orbit);

    Gu::MatrixF lookAt;
    Gu::LookAtF(lookAt, orbit.pos[0], orbit.pos[1], orbit.pos[2], orbit.target[0], orbit.target[1], orbit.target[2],
                0.0f, 1.0f, 0.0f);
    const Mat4 base = ToMat4(lookAt);
    state.chr->Pose(base);
    if (state.gun != nullptr) {
        state.gun->Pose(base);
    }
}

// What lvlRender() emits ahead of every front-end constructor, with this
// screen's view settings (viSetUseZBuf(TRUE), and the projection built from
// viSetFovY/viSetZRange/viSetAspect). The game loop's own black fill stands in
// for the constructor's viFillScreen, and the z-buffer and framebuffer sit at
// placeholder addresses, as the loop's clear does.
Gfx* FramePrologue(Gfx* dl, State& state) {
    using namespace Frigate::F3dex;
    // viClearZBufCurrentPlayer(): zbufInit(), then zbufClearCurrentPlayer().
    gDPPipeSync(dl++);
    gDPSetDepthImage(dl++, reinterpret_cast<void*>(uintptr_t{ kZBufferAddress }));
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_NOOP | G_RM_NOOP2);
    gDPSetColorImage(dl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, kMenuScreenWidth,
                     reinterpret_cast<void*>(uintptr_t{ kZBufferAddress }));
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_FILL);
    gDPSetFillColor(dl++, (GPACK_ZDZ(G_MAXFBZ, 0) << 16) | GPACK_ZDZ(G_MAXFBZ, 0));
    gDPSetScissor(dl++, G_SC_NON_INTERLACE, 0, 0, kMenuScreenWidth, kMenuScreenHeight);
    gDPFillRectangle(dl++, 0, 0, kMenuScreenWidth - 1, kMenuScreenHeight - 1);
    gDPPipeSync(dl++);

    // viSetupCurrentPlayerView(). The modelview loads first: the interpreter
    // multiplies a projection load by the top of the modelview stack, which is
    // empty until the first modelview load. The projection's perspective
    // normalisation (gSPPerspNormalize) is the interpreter's own.
    *dl++ = Viewport(&state.viewport);
    *dl++ = Matrix(&state.view, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
    *dl++ = Matrix(&state.projection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
    gDPSetColorImage(dl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, kMenuScreenWidth,
                     reinterpret_cast<void*>(uintptr_t{ kFramebufferAddress }));

    gDPSetScissor(dl++, G_SC_NON_INTERLACE, 0, 0, kMenuScreenWidth, kMenuScreenHeight);
    return dl;
}

// constructor_menu18_displaycast().
Gfx* Construct(Gfx* dl, MenuStateMachine& menu) {
    State& state = GetState();
    if (state.font == nullptr || state.strings == nullptr) {
        return dl;
    }
    using namespace Frigate::F3dex;
    const Cast::Member& member = Cast::Members()[static_cast<size_t>(state.front->introCharacterIndex)];
    const float fade = Cast::Fade(menu.MenuTimer());

    if (state.chr != nullptr) {
        Cast::Orbit orbit = Cast::OrbitCamera(state.path, static_cast<float>(menu.MenuTimer()) / Cast::kLength);
        PoseModels(state, menu, orbit);

        ModelRenderParams params;
        params.zbufferEnabled = true;
        // The decomp sets PROP_TYPE_EXPLOSION, but its render code compares the
        // prop type against the enum plus one, so the 7 stored is the viewer's.
        params.propType = kRenderPropViewer;
        params.shadowLevel = 0; // sub_GAME_7F073FC8(0)
        std::vector<std::shared_ptr<Model::ModelInstance>> models{ state.chr };
        if (state.gun != nullptr) {
            models.push_back(state.gun);
        }
        state.draw = Model::BuildJointDraw(state.resourceManager, models, params);
    }

    dl = FramePrologue(dl, state);
    // gSPNumLights, the two gelogolight lights and gSPLookAt.
    gSPNumLights(dl++, NUMLIGHTS_1);
    gSPLight(dl++, &state.lights.l[0], LIGHT_1);
    gSPLight(dl++, &state.lights.a, LIGHT_1 + 1);
    gSPLookAt(dl++, &state.lookAt);
    if (state.draw) {
        for (const Gfx& command : state.draw->commands) {
            *dl++ = command;
        }
    }

    dl = DrawFade(dl, fade);
    dl = Text::SetupRdp(dl);
    if (!state.front->fullActorIntro) {
        dl = DrawLine(dl, state, member.text1, kTextTop[0], fade);
    }
    dl = DrawLine(dl, state, member.text2, kTextTop[1], fade);
    return DrawLine(dl, state, member.text3, kTextTop[2], fade);
}

} // namespace

void RegisterCastScreen(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                        const std::shared_ptr<FrontState>& front) {
    GetState().front = front;
    menu.RegisterMenu(MenuId::DisplayCast,
                      {
                          .onUpdate = Update,
                          .onInit = [resourceManager](MenuStateMachine& m) { Init(resourceManager, m); },
                          .onInterface = Interface,
                          .onConstruct = Construct,
                      });
}

} // namespace Frigate::Game::Menu
