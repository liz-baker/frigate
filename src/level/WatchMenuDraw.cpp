#include <algorithm>
#include <cmath>

#include "game/TextRender.h"
#include "gfx/Gu.h"
#include "gfx/GbiF3dex.h"
#include "level/Lang.h"
#include "level/WatchMenu.h"

namespace Frigate::Level {

namespace Text = Frigate::Game::Text;

namespace {

constexpr float kTau = 6.2831855f;

// The line height of the inventory page's list.
constexpr int32_t kListLine = 12;

// The US positions of the pages' text (options.h).
constexpr int32_t kLeft = 64;
constexpr int32_t kRow1 = 80;
constexpr int32_t kRowStep = 15;
constexpr int32_t kMissionStatusY = 0x41;
constexpr int32_t kVersionY = 0x31;
constexpr int32_t kMusicY = 0x25;
constexpr int32_t kEffectsY = 0x3B;
// OPTLABELS_*: the labels around the controller.
constexpr int32_t kLabelRow1 = 0x52;
constexpr int32_t kLabelRow2 = 0x6B;
constexpr int32_t kLabelPitch = 0x19;
constexpr int32_t kLabelColumnReturn = 0x4B;
constexpr int32_t kLabelHint = 0xC3;
// Where the dual controller layout puts its labels.
constexpr int32_t kDualWeaponY = 167;
constexpr int32_t kDualActionY = 149;
constexpr int32_t kDualMoveY = 203;
constexpr int32_t kDualFireY = 185;

// The colours the pages draw their text in.
constexpr uint32_t kGreen = 0x00FF00B0;
constexpr uint32_t kDimGreen = 0x00AA00B0;
constexpr uint32_t kPaleGreen = 0xA0FFA0F0;
constexpr uint32_t kFadedGreen = 0x00800080;
constexpr uint32_t kSelectedOutline = 0x7000A0;
constexpr uint32_t kWhite = 0xFFFFFFFF;
constexpr uint32_t kLabelBackground = 0x3000B0;
constexpr uint32_t kLabelColour = 0xAA00B0;

// Option bank slots (LOPTIONS).
namespace Opt {
constexpr int32_t Fire = 0x00;
constexpr int32_t Aim = 0x01;
constexpr int32_t Action = 0x02;
constexpr int32_t Weapon = 0x03;
constexpr int32_t Move = 0x05;
constexpr int32_t Look = 0x06;
constexpr int32_t MoveSight = 0x08;
constexpr int32_t Abort = 0x24;
constexpr int32_t Confirm = 0x25;
constexpr int32_t Cancel = 0x26;
constexpr int32_t MissionStatus = 0x27;
constexpr int32_t Complete = 0x28;
constexpr int32_t Incomplete = 0x29;
constexpr int32_t QWatch = 0x2B;
constexpr int32_t Down = 0x2C;
constexpr int32_t Up = 0x2D;
constexpr int32_t SidestepRight = 0x2E;
constexpr int32_t SidestepLeft = 0x2F;
constexpr int32_t Forward = 0x30;
constexpr int32_t Back = 0x31;
constexpr int32_t ControlStyle = 0x32;
constexpr int32_t Controller = 0x33;
constexpr int32_t Controllers = 0x34;
constexpr int32_t Music = 0x35;
constexpr int32_t Effects = 0x36;
constexpr int32_t Failed = 0x37;
constexpr int32_t Background = 0x38;
constexpr int32_t MBriefing = 0x39;
constexpr int32_t QBranch = 0x3A;
constexpr int32_t Moneypenny = 0x3B;
constexpr int32_t MissionObjectives = 0x3C;
} // namespace Opt

// game_control_styles: each style's row of ten strings, the style's own name
// first.
constexpr std::array<std::array<int32_t, 10>, 8> kControlStyles = { {
    { 0x09, 0x03, 0x02, 0x00, 0x01, 0x01, 0x06, 0x06, 0x04, 0x05 },
    { 0x0A, 0x03, 0x02, 0x00, 0x01, 0x01, 0x05, 0x05, 0x04, 0x06 },
    { 0x0B, 0x00, 0x02, 0x01, 0x03, 0x03, 0x06, 0x06, 0x04, 0x05 },
    { 0x0C, 0x00, 0x02, 0x01, 0x03, 0x03, 0x05, 0x05, 0x04, 0x06 },
    { 0x0D, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 },
    { 0x0E, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 },
    { 0x0F, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 },
    { 0x10, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 },
} };

// game_options_entries: each option's strings (its name, then its values,
// with 0 for no third value) and whether it has a third value.
struct OptionEntry {
    int32_t name;
    int32_t values[3];
};
constexpr std::array<OptionEntry, 8> kOptionEntries = { {
    { 0x11, { 0x1C, 0x1B, 0 } },
    { 0x12, { 0x1A, 0x19, 0 } },
    { 0x14, { 0x1E, 0x1D, 0 } },
    { 0x15, { 0x1A, 0x19, 0 } },
    { 0x13, { 0x1A, 0x19, 0 } },
    { 0x16, { 0x1A, 0x19, 0 } },
    { 0x17, { 0x1F, 0x20, 0x21 } },
    { 0x18, { 0x22, 0x23, 0 } },
} };

Mat4 ToMat4(const Mtxf& matrix) {
    Mat4 out{};
    std::copy(&matrix.m[0][0], &matrix.m[0][0] + 16, &out.m[0][0]);
    return out;
}

Mtxf ToMtxf(const Gu::MatrixF& matrix) {
    Mtxf out{};
    std::copy(&matrix[0][0], &matrix[0][0] + 16, &out.m[0][0]);
    return out;
}

int16_t S16(int32_t value) {
    return static_cast<int16_t>(value);
}

} // namespace

Vtx_t* WatchMenu::NewVertices() {
    return Frame().vertices.emplace_back().data();
}

Mtx::FixedMtx* WatchMenu::NewMatrix(const Mtx::FixedMtx& value) {
    return &Frame().matrices.emplace_back(value);
}

std::string_view WatchMenu::OptionText(int32_t slot) const {
    return links_.host->Text(StringId(TextBank::Options, slot));
}

std::string_view WatchMenu::StyleText(int32_t style, int32_t column) const {
    return OptionText(kControlStyles[static_cast<size_t>(std::clamp(style, 0, 7))][static_cast<size_t>(column)]);
}

void WatchMenu::ColourTabs() {
    // set_page_rectangle_colors.
    for (Vtx_t& vertex : tabVertices_) {
        vertex.cn[0] = 0x20;
        vertex.cn[1] = 0x70;
        vertex.cn[2] = 0x20;
    }
    for (int32_t i = page_ * 4; i <= (page_ * 4) + 3; i++) {
        Vtx_t& vertex = tabVertices_[static_cast<size_t>(i)];
        if (itemSelected_) {
            vertex.cn[0] = 0x30;
            vertex.cn[1] = 0xA0;
            vertex.cn[2] = 0x30;
        } else {
            vertex.cn[0] = 0x50;
            vertex.cn[1] = 0xF0;
            vertex.cn[2] = 0x50;
        }
    }
}

Gfx* WatchMenu::Render(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm, const Mtx::FixedMtx* matrix, bool open,
                       const WatchMenuFrame& frame) {
    ColourTabs();

    if (open) {
        outsideMenu_ = false;
        levelDrawn_ = false;

        // A or Z selects the page's item, or lets it go, on any page but the
        // inventory (which equips with them).
        if (page_ != WatchPage::Inventory && Pressed(Buttons::Z | Buttons::A)) {
            BeepToggleSelection();
        }

        switch (page_) {
            case WatchPage::MissionStatus:
                dl = DrawStatusPage(dl, rm, matrix, frame);
                break;
            case WatchPage::Inventory:
                dl = DrawInventoryPage(dl, rm, matrix, frame);
                break;
            case WatchPage::ControlOptions:
                dl = DrawControlPage(dl, rm, matrix, frame);
                break;
            case WatchPage::GameOptions:
                dl = DrawGamePage(dl, matrix, frame);
                break;
            case WatchPage::MissionBriefing:
                dl = DrawBriefingPage(dl, matrix, frame);
                break;
            default:
                break;
        }
    } else {
        levelDrawn_ = true;
        outsideMenu_ = true;
        dl = DrawFace(dl, matrix, true, frame);
    }
    return dl;
}

// sub_GAME_7F0A33F8: the vertices of the face's disc, as pairs on either side
// of its middle (and, for a fan, its centre first).
static Vtx_t* MakeDisc(Vtx_t* vertices, int32_t count, float scale, bool fan) {
    const auto set = [](Vtx_t* vertex, int32_t x, int32_t z, uint8_t r, uint8_t g, uint8_t b) {
        vertex->ob[0] = S16(x);
        vertex->ob[1] = 0;
        vertex->ob[2] = S16(z);
        vertex->flag = 0;
        vertex->tc[0] = 0;
        vertex->tc[1] = 0;
        vertex->cn[0] = r;
        vertex->cn[1] = g;
        vertex->cn[2] = b;
        vertex->cn[3] = 0xB0;
    };
    if (fan) {
        set(vertices, 1, 0, 0, 0x2C, 0);
        vertices++;
    }
    for (int32_t i = 7; i <= count - 7; i += 2) {
        const float angle = (static_cast<float>(i) * 3.1415927f) / static_cast<float>(count);
        const auto sine = static_cast<int16_t>(std::sin(angle) * 520.0f * scale);
        const auto cosine = static_cast<int16_t>(std::cos(angle) * 520.0f * scale);
        const auto r = static_cast<uint8_t>(0 - (std::cos(angle) * 0));
        const auto g = static_cast<uint8_t>(44.0f - (std::cos(angle) * 20.0f));
        set(vertices, 1 + sine, -cosine, r, g, r);
        vertices++;
        if (i != 0 && i < count) {
            set(vertices, 1 + -sine, -cosine, r, g, r);
            vertices++;
        }
    }
    return vertices;
}

// draw_watch_background.
static Gfx* EmitDisc(Gfx* dl, Vtx_t* vertices, bool fan) {
    using namespace Frigate::F3dex;
    if (fan) {
        Vtx_t* center = vertices;
        Vtx_t* ring = vertices + 1;
        *dl++ = Vertices(&ring[14], 4, 0);
        *dl++ = Vertices(center, 1, 4);
        *dl++ = Triangle(2, 4, 3);
        for (int32_t i = 7; i >= 0; i--) {
            *dl++ = Vertices(&ring[2 * i], 4, 0);
            *dl++ = Vertices(center, 1, 4);
            *dl++ = Triangle(0, 4, 2);
            *dl++ = Triangle(1, 3, 4);
        }
        *dl++ = Triangle(0, 1, 4);
    } else {
        for (int32_t i = 0; i < 8; i++) {
            *dl++ = Vertices(vertices, 4, 0);
            *dl++ = Triangle(0, 1, 2);
            *dl++ = Triangle(1, 2, 3);
            vertices += 2;
        }
    }
    return dl;
}

Gfx* WatchMenu::DrawFace(Gfx* dl, const Mtx::FixedMtx* matrix, bool unfold, const WatchMenuFrame& frame) {
    // draw_background_health_and_armor. `unfold` is its zoom_squish: the face
    // opens up as the arm comes up and closes as it goes down.
    using namespace Frigate::F3dex;
    constexpr int32_t kDiscVertices = 30;
    (void)frame;

    Vtx_t* outer = NewVertices();
    Vtx_t* inner = NewVertices();

    gDPPipeSync(dl++);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_XLU_SURF | G_RM_AA_XLU_SURF2);
    gDPSetCombineMode(dl++, G_CC_SHADE, G_CC_SHADE);
    gDPSetPrimColor(dl++, 0, 0, 0xE6, 0xE6, 0xE6, 0x00);
    *dl++ = Matrix(matrix, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);

    float scale = 1.0f;
    if (links_.owner->ZoomRunning()) {
        scale = links_.owner->PageZoomScale();
    }
    if (unfold) {
        scale = 0.05f;
        green_ = 0xE0;
        const int32_t state = links_.owner->State();
        if (state == 4 || state == 6) {
            scale = links_.owner->UnfoldScale();
        }
    }

    Gu::MatrixF quarter;
    Gu::ScaleF(quarter, 0.25f, 0.25f, 0.25f);
    *dl++ = Matrix(NewMatrix(Mtx::ToFixedMtx(quarter)), Fast::F3DEX_G_MTX_MODELVIEW);

    if (!unfold) {
        *dl++ = ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH);
        dl = links_.hud->EmitWatchGauges(dl);
    }

    Gu::MatrixF squash;
    Gu::ScaleF(squash, 1.0f, 1.0f, scale);
    *dl++ = Matrix(NewMatrix(Mtx::ToFixedMtx(squash)), Fast::F3DEX_G_MTX_MODELVIEW);

    if (unfold) {
        *dl++ = ClearGeometryMode(Fast::F3DEX_G_CULL_BOTH);
        dl = links_.hud->EmitWatchGauges(dl);
    }

    // The disc's rim.
    MakeDisc(outer, kDiscVertices, 0.92f, false);
    gDPPipeSync(dl++);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineMode(dl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(dl++, 0, 0, 0x00, 0xFF, 0x00, 0x00);
    dl = EmitDisc(dl, outer, false);
    gDPPipeSync(dl++);

    // The green of the disc, which the static breaks into a fuzzy fill.
    const bool fan = green_ >= 0xE0;
    MakeDisc(inner, kDiscVertices, 0.9f, fan);
    if (!fan) {
        *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_PCL_SURF | G_RM_AA_PCL_SURF2);
    } else {
        *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_XLU_SURF | G_RM_AA_XLU_SURF2);
    }
    gDPSetCombineMode(dl++, G_CC_SHADE, G_CC_SHADE);
    dl = EmitDisc(dl, inner, fan);

    // The page tabs.
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineMode(dl++, G_CC_SHADE, G_CC_SHADE);
    for (int32_t page = 0; page < WatchPage::Count; page++) {
        *dl++ = Vertices(tabVertices_.data() + (page * 4), 4, 0);
        *dl++ = Triangle(0, 1, 2);
        *dl++ = Triangle(1, 2, 3);
    }

    for (int32_t i = 0; i < kDiscVertices; i++) {
        inner[i].cn[3] = static_cast<uint8_t>(green_);
        outer[i].cn[3] = static_cast<uint8_t>(green_);
    }

    if (green_ < 0xE0) {
        // The scanline that climbs the face while it is in static, as wide as
        // the face is at its height.
        const auto half = static_cast<int32_t>(std::sqrt(213444.0f - static_cast<float>(scanlineY_ * scanlineY_)));
        Vtx_t* line = staticVertices_.data();
        int32_t index = 0;
        for (int32_t z = 0; z != 8; z += 4) {
            for (int32_t side = -1; side != 3; side += 2) {
                Vtx_t& vertex = line[index++];
                vertex.ob[0] = S16(half * side);
                vertex.ob[1] = 0;
                vertex.ob[2] = S16(z + scanlineY_);
                vertex.flag = 0;
                vertex.tc[0] = 0;
                vertex.tc[1] = 0;
                vertex.cn[0] = 0;
                vertex.cn[1] = 0xA0;
                vertex.cn[2] = 0;
                vertex.cn[3] = static_cast<uint8_t>(scanlineAlpha_);
            }
        }
        *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_XLU_SURF | G_RM_AA_XLU_SURF2);
        *dl++ = Vertices(staticVertices_.data(), 4, 0);
        *dl++ = Triangle(0, 1, 2);
        *dl++ = Triangle(1, 2, 3);
    }
    return dl;
}

Gfx* WatchMenu::DrawLabel(Gfx* dl, const WatchMenuFrame& frame, int32_t x, int32_t y, std::string_view text,
                          uint32_t colour, bool outlined, uint32_t outlineColour, bool centre, bool drawBackground,
                          uint32_t backgroundColour, bool rightAlign) {
    // draw_options_labels.
    using namespace Frigate::F3dex;
    const Font* font = links_.hud->GothicFont();
    if (font == nullptr) {
        return dl;
    }
    const Text::Extent extent = Text::Measure(*font, text, 10);
    int32_t textX = x;
    if (centre) {
        textX = x - (extent.width / 2);
    } else if (rightAlign) {
        textX = x - extent.width;
    }
    const int32_t right = textX + extent.width;
    const int32_t bottom = y + extent.height;

    // The static makes the text more apt to drop out.
    if (green_ < 0xE0) {
        *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_PCL_SURF | G_RM_AA_PCL_SURF2);
    } else {
        *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_XLU_SURF | G_RM_AA_XLU_SURF2);
    }
    if (drawBackground) {
        dl = Text::FillRect(dl, textX - 1, (y + (outlined ? 1 : 0)) + 1, right + 1, bottom + 1, backgroundColour);
    }
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_AA_XLU_SURF | G_RM_AA_XLU_SURF2);

    if (!outlined) {
        dl = Text::Render(dl, &textX, &y, text, *font, colour, extent.width, extent.height, 0, 10);
    } else {
        dl = Text::RenderOutlined(dl, &textX, &y, text, *font, colour, outlineColour, extent.width + 1, extent.height,
                                  0, 10);
    }
    (void)frame;
    return dl;
}

Gfx* WatchMenu::DrawAbortConfirm(Gfx* dl, const WatchMenuFrame& frame) {
    // draw_abort_cancel_confirm.
    const Font* font = links_.hud->GothicFont();
    if (font == nullptr) {
        return dl;
    }
    const std::string_view abort = OptionText(Opt::Abort);
    const std::string_view confirm = OptionText(Opt::Confirm);
    const std::string_view cancel = OptionText(Opt::Cancel);
    int32_t abortX = 0x51;
    int32_t confirmX = 0xBD;
    int32_t cancelX = 0x88;
    int32_t abortY = 0x4C;
    int32_t confirmY = 0x4C;
    int32_t cancelY = 0x4C;
    const int32_t viX = frame.hud.vi.x;
    const int32_t viY = frame.hud.vi.y;

    if (itemSelected_) {
        if (!abortConfirmed_) {
            if (StickX() >= 0x2E || Held(0x111)) {
                abortConfirmed_ = true;
            }
        } else if (StickX() < -0x2D || Held(0x222)) {
            abortConfirmed_ = false;
        }
    }

    const Text::Extent extent = Text::Measure(*font, abort);
    if (itemSelected_) {
        dl = Text::Render(dl, &abortX, &abortY, abort, *font, kPaleGreen, extent.width, extent.height, 0, 0);
        if (abortConfirmed_) {
            dl = Text::RenderOutlined(dl, &confirmX, &confirmY, confirm, *font, kWhite, kSelectedOutline, viX, viY, 0,
                                      0);
            dl = Text::Render(dl, &cancelX, &cancelY, cancel, *font, 0xFF00B0, viX, viY, 0, 0);
        } else {
            dl = Text::Render(dl, &confirmX, &confirmY, confirm, *font, 0xFF00B0, viX, viY, 0, 0);
            dl = Text::RenderOutlined(dl, &cancelX, &cancelY, cancel, *font, kWhite, kSelectedOutline, viX, viY, 0, 0);
        }
    } else {
        dl = Text::Render(dl, &abortX, &abortY, abort, *font, kFadedGreen, extent.width, extent.height, 0, 0);
        dl = Text::Render(dl, &confirmX, &confirmY, confirm, *font, kFadedGreen, viX, viY, 0, 0);
        dl = Text::Render(dl, &cancelX, &cancelY, cancel, *font, kFadedGreen, viX, viY, 0, 0);
    }
    return dl;
}

Gfx* WatchMenu::DrawMissionStatusText(Gfx* dl, const WatchMenuFrame& frame) {
    // draw_text_mission_status.
    const Font* font = links_.hud->GothicFont();
    if (font == nullptr) {
        return dl;
    }
    const std::string_view label = OptionText(Opt::MissionStatus);
    std::string_view status;
    uint32_t colour;
    if (links_.host->AllObjectivesComplete()) {
        colour = 0xFF00B0;
        status = OptionText(Opt::Complete);
    } else {
        colour = incompleteColour_;
        status = OptionText(Opt::Incomplete);
    }

    dl = Text::SetupRdp(dl);
    Text::Extent extent = Text::Measure(*font, label);
    int32_t x = 0x51;
    int32_t y = kMissionStatusY;
    dl = Text::Render(dl, &x, &y, label, *font, 0xFF00B0, extent.width, extent.height, 0, 0);

    x = x + extent.width + 0 + 4;
    y = y - extent.height;
    extent = Text::Measure(*font, status);
    dl = Text::Render(dl, &x, &y, status, *font, colour, extent.width, extent.height, 0, 0);
    return DrawAbortConfirm(dl, frame);
}

Gfx* WatchMenu::DrawVersionText(Gfx* dl, const WatchMenuFrame& frame) {
    // draw_text_q_watch_v201_beta.
    (void)frame;
    const Font* font = links_.hud->GothicFont();
    if (font == nullptr) {
        return dl;
    }
    const std::string_view text = OptionText(Opt::QWatch);
    int32_t x = 0x65;
    int32_t y = kVersionY;
    dl = Text::SetupRdp(dl);
    const Text::Extent extent = Text::Measure(*font, text);
    return Text::Render(dl, &x, &y, text, *font, 0xFF00B0, extent.width, extent.height, 0, 0);
}

Gfx* WatchMenu::DrawStatusPage(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm, const Mtx::FixedMtx* matrix,
                               const WatchMenuFrame& frame) {
    // draw_watch_mission_status_page.
    dl = DrawFace(dl, matrix, false, frame);
    if (!links_.owner->ZoomRunning()) {
        dl = DrawVersionText(dl, frame);
        dl = DrawMissionStatusText(dl, frame);
        dl = DrawHandItemAndAmmo(dl, rm, frame);
    }
    return dl;
}

Gfx* WatchMenu::SetItemRenderState(Gfx* dl, const WatchMenuFrame& frame) {
    // sub_GAME_7F0A6EE8.
    using namespace Frigate::F3dex;
    *dl++ = SetGeometryMode(Fast::F3DEX_G_CULL_BACK);
    *dl++ = SetOtherModeH(G_MDSFT_CYCLETYPE, 2, G_CYC_1CYCLE);
    *dl++ = SetOtherModeH(G_MDSFT_PIPELINE, 1, G_PM_1PRIMITIVE);
    gDPSetScissor(dl++, G_SC_NON_INTERLACE, 0, 0, frame.hud.vi.x, frame.hud.vi.y);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLOD, 1, G_TL_TILE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTLUT, 2, G_TT_NONE);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTDETAIL, 2, G_TD_CLAMP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTPERSP, 1, G_TP_PERSP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTFILT, 2, G_TF_BILERP);
    *dl++ = SetOtherModeH(G_MDSFT_TEXTCONV, 3, G_TC_FILT);
    gDPSetCombineLERP(dl++, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE, 0, 0, 0, SHADE);
    *dl++ = SetOtherModeH(G_MDSFT_ALPHACOMPARE, 2, G_CK_NONE);
    *dl++ = SetOtherModeL(G_MDSFT_ALPHACOMPARE, 2, G_AC_NONE);
    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_OPA_SURF | G_RM_OPA_SURF2);
    *dl++ = SetOtherModeH(G_MDSFT_RGBDITHER, 2, G_CD_MAGICSQ);
    return dl;
}

Gfx* WatchMenu::DrawHandItemAndAmmo(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm,
                                    const WatchMenuFrame& frame) {
    // draw_current_hand_item_and_ammo.
    using namespace Frigate::F3dex;
    const Font* font = links_.hud->GothicFont();
    const int32_t index = equippedItem_;
    const int32_t item = ItemWeapon(index);
    const WatchItem& watch = links_.weapons->Table().Watch(item);

    // get_45_degree_angle: the field of view the item is seen with.
    constexpr float kFovY = 45.0f;
    // LEFTOVERDEBUG's aspect for the status page.
    constexpr float kAspect = 1.33333337f;

    Gu::MatrixF perspective;
    Gu::PerspectiveF(perspective, kFovY, kAspect, 10.0f, 10000.0f, 1.0f);
    *dl++ = Matrix(NewMatrix(Mtx::ToFixedMtx(perspective)), Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);

    Mtxf placed = RotationY((watch.rotY * kTau) / 360.0f);
    const Mtxf tilt = RotationZ(kTau - ((watch.rotX * kTau) / 360.0f));
    placed = Multiply(tilt, placed);
    const Mtxf look = LookAtTarget({ watch.statusZ, watch.statusX, watch.statusY }, { 0.0f, watch.statusX, watch.statusY },
                                   { 0.0f, 1.0f, 0.0f });
    placed = Multiply(look, placed);

    dl = SetItemRenderState(dl, frame);
    if (green_ < 0xE0) {
        dl = DrawItemModel(dl, rm, item, placed, green_ + 1, 0x64DC6428, frame.lookAt);
    } else {
        dl = DrawItemModel(dl, rm, item, placed, 0xFF, 0x64DC6428, frame.lookAt);
    }

    dl = Text::SetupRdp(links_.hud->RenderWatchAmmo(dl, frame.hud));
    if (font != nullptr) {
        // LEFTOVERDEBUG's rows.
        int32_t x = 0x60;
        int32_t y = 0xA0;
        const std::string_view first = ItemName(index, 1);
        Text::Extent extent = Text::Measure(*font, first);
        dl = Text::Render(dl, &x, &y, first, *font, 0xFF00B0, extent.width, extent.height, 0, 0);
        y = 0xAA;
        const std::string_view second = ItemName(index, 2);
        extent = Text::Measure(*font, second);
        dl = Text::Render(dl, &x, &y, second, *font, 0xFF00B0, extent.width, extent.height, 0, 0);
    }
    return dl;
}

Gfx* WatchMenu::DrawInventoryPage(Gfx* dl, const std::shared_ptr<Ship::ResourceManager>& rm,
                                  const Mtx::FixedMtx* matrix, const WatchMenuFrame& frame) {
    // draw_watch_inventory_page.
    using namespace Frigate::F3dex;
    dl = DrawFace(dl, matrix, false, frame);
    if (links_.owner->ZoomRunning()) {
        return dl;
    }

    const int32_t item = ItemWeapon(curItemIndex_);
    const WatchItem& watch = links_.weapons->Table().Watch(item);
    constexpr float kFovY = 45.0f;
    // WATCH_PERSPECTIVE_ASPECT.
    constexpr float kAspect = 1.3333334f;

    Gu::MatrixF perspective;
    Gu::PerspectiveF(perspective, kFovY, kAspect, 10.0f, 10000.0f, 1.0f);
    *dl++ = Matrix(NewMatrix(Mtx::ToFixedMtx(perspective)), Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);

    Mtxf placed = RotationY((watch.rotY * kTau) / 360.0f);
    const Mtxf tilt = RotationZ(kTau - ((watch.rotX * kTau) / 360.0f));
    placed = Multiply(tilt, placed);
    // The item turns about its own axis as time passes.
    const float cosine = std::cos(itemAngle_) * watch.listZ;
    const float sine = std::sin(itemAngle_) * watch.listZ;
    const Mtxf look = LookAtTarget({ cosine, watch.listY, sine + watch.listX }, { 0.0f, watch.listY, watch.listX },
                                   { 0.0f, 1.0f, 0.0f });
    placed = Multiply(look, placed);

    dl = DrawItemModel(SetItemRenderState(dl, frame), rm, item, placed, 0x40, 0xA0FFA03C, frame.lookAt);

    const Font* font = links_.hud->GothicFont();
    if (font == nullptr) {
        return dl;
    }

    // The list of every item's name, scrolled to the current one.
    constexpr int32_t kBaseY = 0x8C;
    std::string names;
    const int32_t count = ItemCount();
    for (int32_t i = 0; i < count; i++) {
        names += ItemName(i, 0);
    }

    if (equipFlash_ > 0) {
        equipFlash_--;
    }
    NavigateInventoryList();

    int32_t x = 0x4E;
    int32_t y = kBaseY;
    const int32_t selectedY = (kListLine * 2) + kBaseY + 1;

    dl = Text::SetupRdp(dl);
    const Text::Extent extent = Text::Measure(*font, names, kListLine);
    dl = Text::FillRect(dl, 0x4E, kBaseY, extent.width + 0x4E, (kListLine * 5) + kBaseY, 0);
    dl = Text::Render(dl, &x, &y, names, *font, kLabelColour, extent.width + 1, kListLine * 5,
                      inventoryTextY_, kListLine);
    dl = Text::FillRect(dl, 0x4B, selectedY, extent.width + 0x52, (kListLine + selectedY) - 2, 0x800050);

    const std::string_view name = ItemName(curItemIndex_, 0);
    dl = Text::SetupRdp(dl);
    if (inventorySettled_) {
        const Text::Extent nameExtent = Text::Measure(*font, name, kListLine);
        x = 0x4E;
        y = (kListLine * 2) + kBaseY;
        if (equipFlash_ == 0) {
            dl = Text::Render(dl, &x, &y, name, *font, kPaleGreen, nameExtent.width, 0x64, 0, kListLine);
        } else {
            dl = Text::RenderOutlined(dl, &x, &y, name, *font, kWhite, kSelectedOutline, nameExtent.width + 1, 0x64, 0,
                                      kListLine);
        }
        EquipSelectedItem();
    }
    return dl;
}

Gfx* WatchMenu::DrawToggleValues(Gfx* dl, const WatchMenuFrame& frame, int32_t y, int32_t option, int32_t state) {
    // draw_toggle_option_values. `state` is 0 for an option that is not
    // highlighted, 1 for the highlighted one and 2 for the one selected.
    uint32_t colour[3] = { kFadedGreen, kFadedGreen, kFadedGreen };
    const OptionEntry& entry = kOptionEntries[static_cast<size_t>(option)];
    int32_t x1 = 0xB4;
    int32_t x2 = 0xE1;

    if (state == 2) {
        ToggleOption(option);
    }
    const uint32_t value = OptionValue(option);
    if (value < 3) {
        colour[value] = state == 2 ? kPaleGreen : kGreen;
    }

    const bool hasThird = entry.values[2] != 0;
    if (!hasThird) {
        x1 = 0xC8;
        x2 = 0xFA;
    }

    dl = DrawLabel(dl, frame, x1, y, OptionText(entry.values[0]), colour[0], false, 0xFFFFFFFF, true, false,
                   kLabelBackground, false);
    dl = DrawLabel(dl, frame, x2, y, OptionText(entry.values[1]), colour[1], false, 0xFFFFFFFF, true, false,
                   kLabelBackground, false);
    if (hasThird) {
        dl = DrawLabel(dl, frame, 0x10E, y, OptionText(entry.values[2]), colour[2], false, 0xFFFFFFFF, true, false,
                       kLabelBackground, false);
    }
    return dl;
}

Gfx* WatchMenu::DrawToggleOptions(Gfx* dl, const WatchMenuFrame& frame) {
    // draw_toggle_options.
    dl = Text::SetupRdp(dl);
    int32_t y = kRow1;
    for (int32_t i = 0; i < 8; i++, y += kRowStep) {
        const std::string_view name = OptionText(kOptionEntries[static_cast<size_t>(i)].name);
        if (i == gameOptionsIndex_ - 2) {
            if (itemSelected_) {
                dl = DrawLabel(dl, frame, kLeft, y, name, kWhite, true, kSelectedOutline, false, false,
                               kLabelBackground, false);
                dl = DrawToggleValues(dl, frame, y, i, 2);
            } else {
                dl = DrawLabel(dl, frame, kLeft, y, name, kPaleGreen, false, 0xFFFFFFFF, false, false,
                               kLabelBackground, false);
                dl = DrawToggleValues(dl, frame, y, i, 1);
            }
        } else {
            dl = DrawLabel(dl, frame, kLeft, y, name, 0xFF00B0, false, 0xFFFFFFFF, false, false, kLabelBackground,
                           false);
            dl = DrawToggleValues(dl, frame, y, i, 0);
        }
    }
    return dl;
}

void WatchMenu::AdjustVolume(uint16_t* volume) {
    // watch_adjust_volume_slider.
    constexpr int32_t kStep = 1024;
    int32_t stick = StickX();
    int32_t adjusted = *volume;

    if (Held(Buttons::RightC | Buttons::R | Buttons::RightPad)) {
        adjusted += kStep;
    } else if (Held(Buttons::LeftC | Buttons::L | Buttons::LeftPad)) {
        adjusted -= kStep;
    }

    if (stick >= 0x47) {
        stick = 0x46;
    } else if (stick < -0x46) {
        stick = -0x46;
    }

    if (stick >= 8) {
        adjusted += ((stick * 0x800) + -0x3800) / 0x46;
    } else if (stick < -7) {
        adjusted += ((stick * 0x800) + 0x3800) / 0x46;
    }

    if (adjusted >= 0x7FFF + 1) {
        *volume = 0x7FFF;
    } else if (adjusted < 0) {
        *volume = 0;
    } else {
        *volume = static_cast<uint16_t>(adjusted);
    }
}

Gfx* WatchMenu::DrawVolumeSlider(Gfx* dl, bool music, int32_t verticalOffset) {
    // draw_fx_volume_slider and draw_music_volume_slider: three rectangles, the
    // bar's unfilled end, its fill and its border.
    using namespace Frigate::F3dex;
    Vtx_t* vertices = NewVertices();
    uint16_t& volume = music ? musicVolume_ : effectsVolume_;

    if (itemSelected_ && gameOptionsIndex_ == (music ? 0 : 1)) {
        AdjustVolume(&volume);
    }
    const float fraction = static_cast<float>(volume) / 32767.0f;
    // TODO(https://github.com/liz-baker/frigate/issues/41): sndApplyVolumeAllSfxSlot
    // and, for the music, musicTrack2ApplySeqpVol (#241).

    *dl++ = SetOtherModeL(G_MDSFT_RENDERMODE, 29, G_RM_XLU_SURF | G_RM_XLU_SURF2);
    for (int32_t rectangle = 0; rectangle < 3; rectangle++) {
        MakeRectangle(vertices + (rectangle * 4), 0, 0, 600, 20, -299, verticalOffset);
        *dl++ = Vertices(vertices + (rectangle * 4), 4, 0);
        *dl++ = Triangle(0, 1, 2);
        *dl++ = Triangle(1, 2, 3);
    }

    // update_volume_slider_verts.
    constexpr int32_t kTransition = 30;
    const int32_t xdiff = vertices[2].ob[0] - vertices[4].ob[0];
    const auto transition = static_cast<int32_t>(static_cast<float>(kTransition) * (1.2f - fraction));
    for (int32_t i = 0; i < 4; i++) {
        vertices[i].cn[0] = 0x20;
        vertices[i].cn[1] = 0x40;
        vertices[i].cn[2] = 0x20;
        vertices[i].cn[3] = 0xE0;
    }
    for (int32_t i = 4; i != 12; i++) {
        Vtx_t& vertex = vertices[i];
        const int32_t filledRight = xdiff + transition;
        if (i < 10) {
            // The filled section brightens with the volume.
            const int32_t rb = static_cast<int32_t>(48.0f * fraction) + 0x40;
            const int32_t g = static_cast<int32_t>(96.0f * fraction) + 0x80;
            vertex.cn[0] = static_cast<uint8_t>(rb);
            vertex.cn[1] = static_cast<uint8_t>(g);
            vertex.cn[2] = static_cast<uint8_t>(rb);
            if (i >= 6) {
                vertex.ob[0] = S16(static_cast<int32_t>(
                    ((static_cast<float>(vertices[4].ob[0]) +
                      ((static_cast<float>(xdiff) + static_cast<float>(transition)) * fraction)) -
                     static_cast<float>(transition))));
                if (vertex.ob[0] < vertices[4].ob[0]) {
                    vertex.ob[0] = vertices[4].ob[0];
                }
            }
        } else {
            vertex.ob[0] = S16(static_cast<int32_t>((static_cast<float>(vertices[4].ob[0]) +
                                                     (static_cast<float>(filledRight) * fraction)) +
                                                    static_cast<float>(transition)));
            if (vertices[2].ob[0] < vertex.ob[0]) {
                vertex.ob[0] = vertices[2].ob[0];
            }
        }
    }
    // The unfilled section starts where the transition ends.
    vertices[1].ob[0] = vertices[10].ob[0];
    vertices[0].ob[0] = vertices[10].ob[0];
    return dl;
}

Gfx* WatchMenu::DrawGamePage(Gfx* dl, const Mtx::FixedMtx* matrix, const WatchMenuFrame& frame) {
    // draw_watch_game_options_page.
    dl = DrawFace(dl, matrix, false, frame);
    if (links_.owner->ZoomRunning()) {
        return dl;
    }
    const Font* font = links_.hud->GothicFont();

    dl = DrawVolumeSlider(dl, true, -275);
    dl = DrawVolumeSlider(dl, false, -205);
    dl = Text::SetupRdp(dl);
    if (font == nullptr) {
        return dl;
    }

    const auto heading = [&](std::string_view text, int32_t y, int32_t index) {
        uint32_t colour = 0xFF00B0;
        int32_t x = kLeft;
        if (gameOptionsIndex_ == index) {
            colour = kPaleGreen;
            if (itemSelected_) {
                colour = kWhite;
            }
        }
        const Text::Extent extent = Text::Measure(*font, text);
        if (itemSelected_ && gameOptionsIndex_ == index) {
            dl = Text::RenderOutlined(dl, &x, &y, text, *font, colour, kSelectedOutline, extent.width + 1,
                                      extent.height, 0, 0);
        } else {
            dl = Text::Render(dl, &x, &y, text, *font, colour, extent.width, extent.height, 0, 0);
        }
    };
    heading(OptionText(Opt::Music), kMusicY, 0);
    heading(OptionText(Opt::Effects), kEffectsY, 1);

    return DrawToggleOptions(dl, frame);
}

Gfx* WatchMenu::DrawBriefingPage(Gfx* dl, const Mtx::FixedMtx* matrix, const WatchMenuFrame& frame) {
    // draw_watch_mission_briefing_page.
    dl = DrawFace(dl, matrix, false, frame);
    if (links_.owner->ZoomRunning()) {
        return dl;
    }
    const Font* font = links_.hud->GothicFont();
    if (font == nullptr) {
        return dl;
    }

    const int32_t viX = frame.hud.vi.x;
    const int32_t viY = frame.hud.vi.y;
    std::string wrapped = "\n";
    std::string pageTitle;

    const std::string_view title = links_.host->BriefingPage(BriefingText::Title);
    dl = Text::SetupRdp(dl);
    Text::Extent extent = Text::Measure(*font, title);

    int32_t boxLeft = ((0xAA - extent.width) / 2) + 0x4B;
    int32_t boxTop = 0x1E;
    const int32_t boxRight = boxLeft + extent.width;
    const int32_t boxBottom = boxTop + extent.height;
    dl = Text::DrawBlackBox(dl, boxLeft, boxTop, boxRight, boxBottom);
    dl = Text::Render(dl, &boxLeft, &boxTop, title, *font, kPaleGreen, extent.width, extent.height, 0, 0);

    boxLeft = 0x41;

    switch (briefIndex_) {
        case BriefIndex::Background:
            pageTitle = OptionText(Opt::Background);
            wrapped = Text::Wrap(0xD2, links_.host->BriefingPage(BriefingText::Overview), *font);
            NavigateBriefing(BriefIndex::Objectives, BriefIndex::M, false);
            break;
        case BriefIndex::M:
            pageTitle = OptionText(Opt::MBriefing);
            wrapped = Text::Wrap(0xD2, links_.host->BriefingPage(BriefingText::M), *font);
            NavigateBriefing(BriefIndex::Background, BriefIndex::Q, true);
            break;
        case BriefIndex::Q:
            pageTitle = OptionText(Opt::QBranch);
            wrapped = Text::Wrap(0xD2, links_.host->BriefingPage(BriefingText::Q), *font);
            NavigateBriefing(BriefIndex::M, BriefIndex::Moneypenny, true);
            break;
        case BriefIndex::Moneypenny:
            pageTitle = OptionText(Opt::Moneypenny);
            wrapped = Text::Wrap(0xD2, links_.host->BriefingPage(BriefingText::Moneypenny), *font);
            NavigateBriefing(BriefIndex::Q, BriefIndex::Objectives, true);
            break;
        case BriefIndex::Objectives: {
            int32_t lineCount = 1;
            int32_t objectiveRow = 0;
            int32_t visibleIndex = 0;

            Text::SetOverlapCorrection(5);
            pageTitle = OptionText(Opt::MissionObjectives);

            const int32_t count = links_.host->ObjectiveCount();
            for (int32_t i = 0; i < count; i++) {
                if (links_.host->ObjectiveDifficulty(i) > links_.host->Difficulty()) {
                    continue;
                }
                const std::string_view objective = links_.host->ObjectiveText(i);
                std::string buffer;
                for (int32_t j = 0; j < lineCount; j++) {
                    buffer += " \n";
                }
                for (int32_t j = 0; j < objectiveRow; j++) {
                    buffer += " \n\n";
                }
                const size_t letterAt = buffer.size();
                buffer += static_cast<char>('a' + visibleIndex);
                buffer += ": ";
                buffer += objective;

                int32_t objY = boxTop + 5;
                int32_t objX = 0x3C;
                dl = Text::Render(dl, &objX, &objY, buffer, *font, kGreen, viX, viY, 0, 10);

                // sub_GAME_7F0AC0E8 counts the lines of the objective, and
                // sub_GAME_7F0AC120 gives the last of them.
                const std::string_view letterText = std::string_view(buffer).substr(letterAt);
                lineCount += static_cast<int32_t>(std::count(letterText.begin(), letterText.end(), '\n'));
                size_t lastLine = 0;
                for (size_t k = 0; k < letterText.size(); k++) {
                    if (letterText[k] == '\n' && k + 1 < letterText.size()) {
                        lastLine = k + 1;
                    }
                }
                extent = Text::Measure(*font, letterText.substr(lastLine), 10);
                if ((extent.width + 0x3C) < (viX / 2)) {
                    lineCount--;
                }

                buffer.clear();
                for (int32_t j = 0; j < lineCount; j++) {
                    buffer += " \n";
                }
                for (int32_t j = 0; j < objectiveRow; j++) {
                    buffer += " \n\n";
                }

                uint32_t colour = kGreen;
                switch (links_.host->ObjectiveStatusOf(i)) {
                    case ObjectiveStatus::Incomplete:
                        buffer += links_.host->Text(StringId(TextBank::Options, Opt::Incomplete));
                        colour = (objectiveGreen_ << 16) | 0x400040FF;
                        break;
                    case ObjectiveStatus::Complete:
                        buffer += links_.host->Text(StringId(TextBank::Options, Opt::Complete));
                        colour = kPaleGreen;
                        break;
                    case ObjectiveStatus::Failed:
                        buffer += links_.host->Text(StringId(TextBank::Options, Opt::Failed));
                        colour = kGreen;
                        break;
                }

                extent = Text::Measure(*font, buffer, 10);
                objY = boxTop + 5;
                objX = 0xAF;
                dl = Text::Render(dl, &objX, &objY, buffer, *font, colour, 0xD2, viY, 0, 10);

                objectiveRow++;
                visibleIndex++;
            }

            Text::SetOverlapCorrection(-1);
            NavigateBriefing(BriefIndex::Moneypenny, BriefIndex::Background, true);
            break;
        }
        default:
            break;
    }

    extent = Text::Measure(*font, pageTitle, 10);
    dl = Text::Render(dl, &boxLeft, &boxTop, pageTitle, *font, kPaleGreen, 0xD2, 0x82, 0, 10);

    boxTop += 5;
    boxLeft = 0x3C;

    extent = Text::Measure(*font, wrapped, 10);
    return Text::Render(dl, &boxLeft, &boxTop, wrapped, *font, kGreen, viX, viY, 0, 10);
}

} // namespace Frigate::Level
