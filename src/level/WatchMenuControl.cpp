#include <algorithm>
#include <cmath>

#include "game/TextRender.h"
#include "gfx/Gu.h"
#include "gfx/GbiF3dex.h"
#include "level/GunDraw.h"
#include "level/Lang.h"
#include "level/ModelHandle.h"
#include "level/WatchMenu.h"
#include "model/RenderState.h"

namespace Frigate::Level {

namespace Text = Frigate::Game::Text;

namespace {

constexpr float kTau = 6.2831855f;

// The text and rows of the control page (options.h and options.c, US).
constexpr int32_t kLeft = 64;
constexpr int32_t kLabelRow1 = 0x52;
constexpr int32_t kLabelRow2 = 0x6B;
constexpr int32_t kLabelPitch = 0x19;
constexpr int32_t kLabelColumnReturn = 0x4B;
constexpr int32_t kLabelHint = 0xC3;
constexpr int32_t kDualWeaponY = 167;
constexpr int32_t kDualActionY = 149;
constexpr int32_t kDualMoveY = 203;
constexpr int32_t kDualFireY = 185;

constexpr uint32_t kPaleGreen = 0xA0FFA0F0;
constexpr uint32_t kWhite = 0xFFFFFFFF;
constexpr uint32_t kSelectedOutline = 0x7000A0;
constexpr uint32_t kLabelBackground = 0x3000B0;
constexpr uint32_t kLabelColour = 0xAA00B0;
constexpr uint32_t kDimGreen = 0x00AA00B0;

// LOPTIONS slots.
constexpr int32_t kOptFire = 0x00;
constexpr int32_t kOptAim = 0x01;
constexpr int32_t kOptAction = 0x02;
constexpr int32_t kOptWeapon = 0x03;
constexpr int32_t kOptMove = 0x05;
constexpr int32_t kOptLook = 0x06;
constexpr int32_t kOptMoveSight = 0x08;
constexpr int32_t kOptDown = 0x2C;
constexpr int32_t kOptUp = 0x2D;
constexpr int32_t kOptSidestepRight = 0x2E;
constexpr int32_t kOptSidestepLeft = 0x2F;
constexpr int32_t kOptForward = 0x30;
constexpr int32_t kOptBack = 0x31;
constexpr int32_t kOptControlStyle = 0x32;
constexpr int32_t kOptController = 0x33;
constexpr int32_t kOptControllers = 0x34;

// Where the controller's buttons float on either side of the face: the
// button positions tables, each a coord for start, joystick, d-pad, C up,
// down, left and right, A, B, L, R and Z (g_1ContButtonPositions, and the left
// and right ones of the two-controller layouts), in that order.
struct ButtonPositions {
    std::array<Vec3, 12> at;
};
constexpr ButtonPositions kSinglePositions = { { {
    { -900.0f, 200.0f, -45.0f },
    { 715.0f, 200.0f, 393.0f },
    { -875.0f, 200.0f, -210.0f },
    { 900.0f, 200.0f, -260.0f },
    { 900.0f, 200.0f, -160.0f },
    { 850.0f, 200.0f, -210.0f },
    { 950.0f, 200.0f, -210.0f },
    { 900.0f, 200.0f, 128.0f },
    { 900.0f, 200.0f, -45.0f },
    { -820.0f, 200.0f, -389.0f },
    { 820.0f, 200.0f, -389.0f },
    { -830.0f, 200.0f, 78.0f },
} } };

// g_2ContLeftButtonPositions and g_2ContRightButtonPositions: the two
// controllers' layout keeps only the stick, A, B and Z on the face; the rest
// are off to one side (2000).
constexpr ButtonPositions kLeftPositions = { { {
    { 2000.0f, 0.0f, 2000.0f },
    { -600.0f, 200.0f, 500.0f },
    { 2000.0f, 0.0f, 2000.0f },
    { 2000.0f, 0.0f, 2000.0f },
    { 2000.0f, 0.0f, 2000.0f },
    { 2000.0f, 0.0f, 2000.0f },
    { 2000.0f, 0.0f, 2000.0f },
    { -600.0f, 200.0f, 240.0f },
    { -600.0f, 200.0f, 110.0f },
    { 2000.0f, 0.0f, 2000.0f },
    { 2000.0f, 0.0f, 2000.0f },
    { -600.0f, 200.0f, 320.0f },
} } };
constexpr ButtonPositions kRightPositions = { { {
    { 2000.0f, 0.0f, 2000.0f },
    { 600.0f, 200.0f, 500.0f },
    { 2000.0f, 0.0f, 2000.0f },
    { 2000.0f, 0.0f, 2000.0f },
    { 2000.0f, 0.0f, 2000.0f },
    { 2000.0f, 0.0f, 2000.0f },
    { 2000.0f, 0.0f, 2000.0f },
    { 600.0f, 200.0f, 240.0f },
    { 600.0f, 200.0f, 110.0f },
    { 2000.0f, 0.0f, 2000.0f },
    { 2000.0f, 0.0f, 2000.0f },
    { 600.0f, 200.0f, 320.0f },
} } };

Mat4 ToMat4(const Mtxf& matrix) {
    Mat4 out{};
    std::copy(&matrix.m[0][0], &matrix.m[0][0] + 16, &out.m[0][0]);
    return out;
}

} // namespace

// sub_GAME_7F0A95C4.
float WatchMenu::Ease(float value, float target, float divisor) {
    if (value < target) {
        value += (target - value) / divisor;
    } else if (target < value) {
        value -= (value - target) / divisor;
    }
    return value;
}

// sub_GAME_7F0A9684: the controller drifts back to rest when it has not been
// touched for 100 ticks, and follows the stick while it is selected.
void WatchMenu::SpinController(int32_t pad) {
    Spin& spin = spin_[static_cast<size_t>(pad)];
    const int32_t stickX = pad == 0 ? input_.stickX : input_.stick2X;
    int32_t count;
    bool zeroed = false;

    if (stickX >= 10 || stickX < -9) {
        if (itemSelected_) {
            count = 0;
            if (controllerIndex_ == 1) {
                zeroed = true;
            }
        }
    }

    if (zeroed) {
        spin.counter = 0;
        count = 0;
    } else {
        count = spin.counter;
        if (count < 100) {
            spin.counter = count + 1;
            count = spin.counter;
        }
    }

    if (count >= 100) {
        spin.speed = Ease(spin.speed, (-spin.angle) / 10.0f, 4.0f);
    } else if (itemSelected_ && controllerIndex_ == 1) {
        spin.speed = Ease(spin.speed, (((-static_cast<float>(stickX)) * 0.2f) * kTau) / 360.0f, 4.0f);
    }
}

Gfx* WatchMenu::DrawControlPage(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm, const Mtx::FixedMtx* matrix,
                                const WatchMenuFrame& frame) {
    // draw_watch_control_options_page.
    dl = DrawFace(dl, matrix, false, frame);
    if (links_.owner->ZoomRunning()) {
        return dl;
    }
    const Font* font = links_.hud->GothicFont();

    dl = DrawController(dl, rm, frame);
    dl = Text::SetupRdp(dl);
    if (font == nullptr) {
        return dl;
    }

    const auto heading = [&](std::string_view text, int32_t y, int32_t index) {
        uint32_t colour = 0xFF00B0;
        int32_t x = kLeft;
        if (controllerIndex_ == index) {
            colour = kPaleGreen;
            if (itemSelected_) {
                colour = kWhite;
            }
        }
        const Text::Extent extent = Text::Measure(*font, text);
        if (itemSelected_ && controllerIndex_ == index) {
            dl = Text::RenderOutlined(dl, &x, &y, text, *font, colour, kSelectedOutline, extent.width + 1,
                                      extent.height, 0, 0);
        } else {
            dl = Text::Render(dl, &x, &y, text, *font, colour, extent.width, extent.height, 0, 0);
        }
    };

    heading(OptionText(kOptControlStyle), 0x1A, 0);
    dl = DrawControlStyleText(dl, frame);

    const bool dual = input_.controllers >= 2 && ControlType() >= 4;
    heading(OptionText(dual ? kOptControllers : kOptController), 0x2B, 1);
    return dl;
}

Gfx* WatchMenu::DrawControlStyleText(Gfx* dl, const WatchMenuFrame& frame) {
    // draw_controller_style_text.
    (void)frame;
    const Font* font = links_.hud->GothicFont();
    if (font == nullptr) {
        return dl;
    }

    std::string text;
    for (int32_t i = 0; i < styleCount_; i++) {
        text += StyleText(i, 0);
    }

    if (itemSelected_ && controllerIndex_ == 0) {
        NavigateControlStyleList();
    }

    int32_t x = 0xAA;
    int32_t y = 0x1A;
    constexpr int32_t kRow = 10;
    const Text::Extent extent = Text::Measure(*font, text, kRow);
    dl = Text::Render(dl, &x, &y, text, *font, kDimGreen, extent.width, kRow, styleOffset_, kRow);

    if (styleSettled_) {
        const std::string_view selected = StyleText(styleIndex_, 0);
        const Text::Extent selectedExtent = Text::Measure(*font, selected, kRow);
        x = 0xAA;
        y = 0x1A;
        dl = Text::Render(dl, &x, &y, selected, *font, kPaleGreen, selectedExtent.width, 0x64, 0, kRow);
    }
    return dl;
}

Gfx* WatchMenu::DrawControllerLabels(Gfx* dl, const WatchMenuFrame& frame) {
    // sub_GAME_7F0A9AB8: the single controller's buttons, labelled with what
    // the style does with each.
    const std::array<int32_t, 10>& row = StyleRow(ControlType());
    std::string_view up;
    std::string_view down;
    if (OptionValue(0) == 1) {
        up = OptionText(kOptUp);
        down = OptionText(kOptDown);
    } else {
        up = OptionText(kOptDown);
        down = OptionText(kOptUp);
    }

    const auto text = [&](size_t column) { return OptionText(row[column]); };
    const auto label = [&](int32_t x, int32_t y, std::string_view string, bool lit, bool rightAlign) {
        if (lit) {
            dl = DrawLabel(dl, frame, x, y, string, kWhite, true, kSelectedOutline, false, false, kLabelBackground,
                           rightAlign);
        } else {
            dl = DrawLabel(dl, frame, x, y, string, kLabelColour, false, 0xFFFFFFFF, false, false, kLabelBackground,
                           rightAlign);
        }
    };

    bool showMoveSight = false;
    dl = Text::SetupRdp(dl);

    label(0x32, kLabelRow1, text(4), Held(Buttons::L), false);
    if (Held(Buttons::L) && row[4] == kOptAim) {
        showMoveSight = true;
    }

    int32_t y = kLabelRow2;
    if (controllerIndex_ != 1 || !itemSelected_ ||
        !Held(Buttons::UpPad | Buttons::DownPad | Buttons::LeftPad | Buttons::RightPad)) {
        label(0x32, y, text(7), false, false);
    } else {
        std::string_view direction;
        if (Held(Buttons::UpPad)) {
            direction = row[7] == kOptMove ? OptionText(kOptForward) : up;
        } else if (Held(Buttons::DownPad)) {
            direction = row[7] == kOptMove ? OptionText(kOptBack) : down;
        } else if (Held(Buttons::LeftPad)) {
            direction = OptionText(kOptSidestepLeft);
        } else {
            direction = OptionText(kOptSidestepRight);
        }
        label(0x32, y, direction, true, false);
    }

    y += kLabelPitch;
    label(0x32, y, text(8), false, false);

    y += kLabelPitch;
    label(0x32, y, text(3), Held(Buttons::Z), false);
    if (Held(Buttons::Z) && row[3] == kOptAim) {
        showMoveSight = true;
    }

    y -= kLabelColumnReturn;
    label(0x10E, y, text(5), Held(Buttons::R), true);
    if (Held(Buttons::R) && row[5] == kOptAim) {
        showMoveSight = true;
    }

    y += kLabelPitch;
    constexpr uint16_t kCButtons = Buttons::UpC | Buttons::DownC | Buttons::LeftC | Buttons::RightC;
    if (controllerIndex_ != 1 || !itemSelected_ || !Held(kCButtons)) {
        label(0x10E, y, text(6), false, true);
    } else {
        const uint16_t buttons = input_.buttons & kCButtons;
        // Exactly one C button down.
        if ((buttons & (buttons - 1U)) == 0) {
            std::string_view direction;
            if (Held(Buttons::UpC)) {
                direction = row[6] == kOptMove ? OptionText(kOptForward) : up;
            } else if (Held(Buttons::DownC)) {
                direction = row[6] == kOptMove ? OptionText(kOptBack) : down;
            } else if (Held(Buttons::LeftC)) {
                direction = OptionText(kOptSidestepLeft);
            } else {
                direction = OptionText(kOptSidestepRight);
            }
            label(0x10E, y, direction, true, true);
        } else {
            label(0x10E, y, text(6), false, true);
        }
    }

    y += kLabelPitch;
    label(0x10E, y, text(2), Held(Buttons::B), true);
    y += kLabelPitch;
    label(0x10E, y, text(1), Held(Buttons::A), true);

    if (showMoveSight) {
        dl = DrawLabel(dl, frame, 0xFA, kLabelHint, OptionText(kOptMoveSight), kWhite, true, kSelectedOutline, false,
                       false, kLabelBackground, true);
    } else {
        dl = DrawLabel(dl, frame, 0xFA, kLabelHint, text(9), kLabelColour, false, 0xFFFFFFFF, false, false,
                       kLabelBackground, true);
    }
    return dl;
}

Gfx* WatchMenu::DrawDualControllerLabels(Gfx* dl, const WatchMenuFrame& frame) {
    // display_text_buttons_dual_control.
    dl = Text::SetupRdp(dl);
    const int32_t type = ControlType();
    const bool plentyOrGalore =
        type == static_cast<int32_t>(ControlConfig::Plenty) || type == static_cast<int32_t>(ControlConfig::Galore);
    const bool plentyOrDomino =
        type == static_cast<int32_t>(ControlConfig::Plenty) || type == static_cast<int32_t>(ControlConfig::Domino);

    const auto label = [&](int32_t x, int32_t y, std::string_view string, bool lit, bool rightAlign) {
        if (lit) {
            dl = DrawLabel(dl, frame, x, y, string, kWhite, true, kSelectedOutline, false, false, kLabelBackground,
                           rightAlign);
        } else {
            dl = DrawLabel(dl, frame, x, y, string, kLabelColour, false, 0xFFFFFFFF, false, false, kLabelBackground,
                           rightAlign);
        }
    };

    label(0x5A, kDualWeaponY, OptionText(kOptWeapon), Held(Buttons::A), false);
    label(0x5A, kDualActionY, OptionText(kOptAction), Held(Buttons::B), false);
    label(0x5A, kDualFireY, OptionText(plentyOrGalore ? kOptFire : kOptAim), Held(Buttons::Z), false);
    label(0x5A, kDualMoveY, OptionText(plentyOrDomino ? kOptMove : kOptLook), false, false);

    const bool second = true;
    (void)second;
    const auto held2 = [this](uint16_t mask) { return (input_.buttons2 & mask) != 0; };
    label(0xE6, kDualWeaponY, OptionText(kOptWeapon), held2(Buttons::A), true);
    label(0xE6, kDualActionY, OptionText(kOptAction), held2(Buttons::B), true);
    label(0xE6, kDualFireY, OptionText(plentyOrGalore ? kOptAim : kOptFire), held2(Buttons::Z), true);
    label(0xE6, kDualMoveY, OptionText(plentyOrDomino ? kOptLook : kOptMove), false, true);
    return dl;
}

Gfx* WatchMenu::DrawController(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm, const WatchMenuFrame& frame) {
    // draw_watch_controller: the controller turning in the middle of the face,
    // its buttons lit as they are pressed, and the labels around it.
    using namespace Frigate::F3dex;
    // US: WATCH_PERSPECTIVE_FOVY and _ASPECT.
    constexpr float kFovY = 50.5f;
    constexpr float kAspect = 1.3333334f;
    const Vec3 position = { 0.0f, 200.0f, -200.0f };
    const bool dual = input_.controllers >= 2 && ControlType() >= 4;

    const auto place = [&](int32_t pad, Vec3 eye, Vec3 target) {
        Spin& spin = spin_[static_cast<size_t>(pad)];
        SpinController(pad);
        spin.angle += (spin.speed * static_cast<float>(frame.speedFrames)) * 0.5f;
        spin.angle = WrapAroundPi(spin.angle);

        const Mtxf zRotation = RotationZ(spin.angle);
        const int32_t stickY = pad == 0 ? input_.stickY : input_.stick2Y;
        if (itemSelected_ && controllerIndex_ == 1) {
            spin.pitch = Ease(spin.pitch, (static_cast<float>(stickY) * kTau) / 360.0f, 4.0f);
        } else {
            spin.pitch = Ease(spin.pitch, 0.0f, 4.0f);
        }
        const Mtxf xRotation = RotationX((-spin.pitch) - 0.78539819f);
        const Mtxf identity = IdentityMtxf();
        const Mtxf first = Multiply(identity, zRotation);
        const Mtxf second = Multiply(first, xRotation);
        const Mtxf model = Multiply(TranslationMtxf(position), second);
        return Multiply(LookAtTarget(eye, target, { 0.0f, 0.0f, -1.0f }), model);
    };

    const auto perspective = [&]() {
        Gu::MatrixF projection;
        Gu::PerspectiveF(projection, kFovY, kAspect, 1000.0f, 3000.0f, 1.0f);
        *dl++ = Matrix(NewMatrix(Mtx::ToFixedMtx(projection)), Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);
    };

    // The left or only controller.
    Mtxf base;
    if (dual) {
        base = place(0, { 495.0f, 2500.0f, 32.0f }, { 495.0f, 0.0f, 32.0f });
    } else {
        base = place(0, { -5.0f, 2000.0f, -168.0f }, { -5.0f, 0.0f, -168.0f });
    }
    perspective();
    dl = SetItemRenderState(dl, frame);
    const int32_t green = green_;
    dl = DrawControllerModel(dl, rm, base, 0, green < 0xE0 ? green - 6 : 0xFF);

    if (dual) {
        base = place(1, { -505.0f, 2500.0f, 32.0f }, { -505.0f, 0.0f, 32.0f });
        perspective();
        dl = SetItemRenderState(dl, frame);
        dl = DrawControllerModel(dl, rm, base, 1, green_ < 0xE0 ? green_ - 6 : 0xFF);
        return DrawDualControllerLabels(dl, frame);
    }
    return DrawControllerLabels(dl, frame);
}

Gfx* WatchMenu::DrawControllerModel(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm, const Mtxf& base,
                                    int32_t pad, int32_t envColour) {
    // watchRenderController, with its buttons animated.
    constexpr int32_t kButtonNodes = 13;
    links_.weapons->SetWatchMenuItem(HandRight, kItemJoypad);
    if (!links_.weapons->HandWithoutItem(HandRight) || links_.weapons->HandItem(HandRight) == kItemUnarmed) {
        return dl;
    }

    std::shared_ptr<Model::ModelInstance>& cached = models_[kItemJoypad];
    if (cached == nullptr && links_.models != nullptr) {
        cached = links_.models->CreateGun(links_.weapons->Table().Item(kItemJoypad).model);
    }
    if (cached == nullptr) {
        return dl;
    }
    Model::ModelInstance& model = *cached;
    if (model.SwitchCount() < kButtonNodes) {
        return dl;
    }

    const int32_t stickX = pad == 0 ? input_.stickX : input_.stick2X;
    const int32_t stickY = pad == 0 ? input_.stickY : input_.stick2Y;
    const uint16_t buttons = pad == 0 ? input_.buttons : input_.buttons2;
    const auto held = [buttons](uint16_t mask) { return (buttons & mask) != 0; };

    const auto origin = [&model](size_t slot) -> Vec3 {
        const ModelNode* node = model.SwitchNode(slot);
        return node != nullptr ? Vec3{ node->origin[0], node->origin[1], node->origin[2] } : Vec3{ 0.0f, 0.0f, 0.0f };
    };

    // The joystick tilts with the stick.
    const auto stickTilt = [&]() {
        const Mtxf z = RotationZ((((-static_cast<float>(stickX)) * kTau) * 0.6f) / 360.0f);
        const Mtxf x = RotationX((((-static_cast<float>(stickY)) * kTau) * 0.6f) / 360.0f);
        return Multiply(x, z);
    };

    model.ResetNodeState();
    std::vector<Mat4> matrices(model.MatrixCount(), IdentityMat4());
    const auto store = [&matrices](size_t index, const Mtxf& matrix) {
        if (index < matrices.size()) {
            matrices[index] = ToMat4(matrix);
        }
    };

    store(0, base);
    for (size_t i = 1; i < 13; i++) {
        Mtxf local;
        if (i == 2) {
            local = stickTilt();
            const Vec3 position = origin(i);
            local.m[3][0] = position[0];
            local.m[3][1] = position[1];
            local.m[3][2] = position[2];
        } else {
            local = TranslationMtxf(origin(i));
        }
        store(i, Multiply(base, local));
    }
    model.PoseWithMatrices(matrices);

    ModelRenderParams params;
    params.zbufferEnabled = true;
    params.passes = 3;
    params.sortJoints = false;
    if (envColour >= 0xFF) {
        // PROP_TYPE_OBJ.
        params.propType = 1;
    } else {
        // PROP_TYPE_PLAYER.
        params.propType = kRenderPropWeapon;
        params.envColour = static_cast<uint32_t>(envColour);
        params.fogColour = 0xFFFFFF00;
    }

    const auto emit = [&]() {
        auto draw = Model::BuildJointDraw(rm, { cached }, params);
        if (draw) {
            Model::JointDraw& kept = Frame().draws.emplace_back(std::move(*draw));
            for (const Gfx& command : kept.commands) {
                *dl++ = command;
            }
        }
    };
    emit();

    // The buttons again, floating to the sides of the face where they light as
    // they are pressed; the controller itself is hidden for this pass.
    model.SetSwitchVisible(13, false);
    const bool dual = input_.controllers >= 2 && ControlType() >= 4;
    (void)dual;

    // The positions: the single controller's, or the left or right of the
    // dual layout.
    ButtonPositions positions = kSinglePositions;
    if (input_.controllers >= 2 && ControlType() >= 4) {
        // The dual layouts leave most of the buttons off the face (2000 is
        // off to one side) and keep the stick and a few others.
        positions = pad == 0 ? kLeftPositions : kRightPositions;
    }

    const Mtxf lookAt = LookAtTarget({ -5.0f, 2000.0f, -168.0f }, { -5.0f, 0.0f, -168.0f }, { 0.0f, 0.0f, -1.0f });
    // sub_GAME_7F06351C: the button's matrix, from its base position, the
    // rotations that tilt it, its place on the face and the view.
    const auto button = [&](const Vec3& buttonBase, const Mtxf& pressed, const Mtxf& fixed, const Vec3& place) {
        Mtxf out = TranslationMtxf(buttonBase);
        out = Multiply(pressed, out);
        out = Multiply(fixed, out);
        out = Multiply(IdentityMtxf(), out);
        out = Multiply(TranslationMtxf(place), out);
        return Multiply(lookAt, out);
    };

    std::vector<Mat4> second(model.MatrixCount(), IdentityMat4());
    const auto store2 = [&second](size_t index, const Mtxf& matrix) {
        if (index < second.size()) {
            second[index] = ToMat4(matrix);
        }
    };
    const Vec3 zero = { 0.0f, 0.0f, 0.0f };
    const Mtxf identity = IdentityMtxf();
    for (size_t i = 0; i < 13; i++) {
        Mtxf pressed = identity;
        Vec3 buttonBase = zero;
        switch (i) {
            case 2: {
                // The joystick.
                const Mtxf tilt = stickTilt();
                Mtxf out = Multiply(TranslationMtxf(positions.at[1]), tilt);
                store2(i, Multiply(lookAt, out));
                continue;
            }
            case 11:
                // R.
                if (held(Buttons::R)) {
                    pressed = RotationY(-0.17453294f);
                }
                store2(i, button(buttonBase, pressed, RotationX(1.0471976f), positions.at[10]));
                break;
            case 4:
                if (held(Buttons::UpC)) {
                    buttonBase[1] += -10.0f;
                }
                store2(i, button(buttonBase, pressed, RotationX(-1.0471976f), positions.at[3]));
                break;
            case 5:
                if (held(Buttons::DownC)) {
                    buttonBase[1] += -10.0f;
                }
                store2(i, button(buttonBase, pressed, RotationX(-1.0471976f), positions.at[4]));
                break;
            case 6:
                if (held(Buttons::LeftC)) {
                    buttonBase[1] += -10.0f;
                }
                store2(i, button(buttonBase, pressed, RotationX(-1.0471976f), positions.at[5]));
                break;
            case 7:
                if (held(Buttons::RightC)) {
                    buttonBase[1] += -10.0f;
                }
                store2(i, button(buttonBase, pressed, RotationX(-1.0471976f), positions.at[6]));
                break;
            case 9:
                // B.
                if (held(Buttons::B)) {
                    buttonBase[1] += -10.0f;
                }
                store2(i, button(buttonBase, pressed, RotationX(-1.0471976f), positions.at[8]));
                break;
            case 8:
                // A.
                if (held(Buttons::A)) {
                    buttonBase[1] += -10.0f;
                }
                store2(i, button(buttonBase, pressed, RotationX(-1.0471976f), positions.at[7]));
                break;
            case 10:
                // L.
                if (held(Buttons::L)) {
                    pressed = RotationY(0.17453294f);
                }
                store2(i, button(buttonBase, pressed, RotationX(1.0471976f), positions.at[9]));
                break;
            case 3: {
                // The d-pad rocks towards the direction pressed.
                Mtxf rock = identity;
                if (held(Buttons::UpPad)) {
                    pressed = RotationX(-0.17453294f);
                } else if (held(Buttons::DownPad)) {
                    pressed = RotationX(0.17453294f);
                }
                if (held(Buttons::LeftPad)) {
                    rock = RotationZ(0.17453294f);
                } else if (held(Buttons::RightPad)) {
                    rock = RotationZ(-0.17453294f);
                }
                pressed = Multiply(rock, pressed);
                store2(i, button(buttonBase, pressed, RotationX(-0.89759791f), positions.at[2]));
                break;
            }
            case 1:
                // Start.
                if (held(Buttons::Start)) {
                    buttonBase[1] += -10.0f;
                }
                store2(i, button(buttonBase, pressed, RotationX(-1.0471976f), positions.at[0]));
                break;
            case 12:
                // Z.
                if (held(Buttons::Z)) {
                    pressed = RotationX(-0.17453294f);
                }
                store2(i, button(buttonBase, pressed, RotationZ(3.1415927f), positions.at[11]));
                break;
            default:
                store2(i, base);
                break;
        }
    }
    model.PoseWithMatrices(second);
    emit();
    return dl;
}

} // namespace Frigate::Level
