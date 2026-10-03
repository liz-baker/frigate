#include "game/menu/FileSelect.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "fast/resource/type/Texture.h"
#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "game/menu/BasicGeometry.h"
#include "game/menu/FileSelectModel.h"
#include "game/menu/FolderBackground.h"
#include "game/menu/FrontCursor.h"
#include "game/menu/FolderScreenSpace.h"
#include "game/menu/MissionFolders.h"
#include "game/menu/PropCamera.h"
#include "game/menu/WalletBond.h"
#include "game/save/SaveProgress.h"
#include "gfx/FixedMtx.h"
#include "gfx/GbiF3dex.h"
#include "gfx/Gu.h"
#include "level/ImageDraw.h"
#include "level/ModelHit.h"
#include "model/JointDraw.h"
#include "model/ModelInstance.h"
#include "resource/type/Font.h"
#include "resource/type/Text.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

namespace Space = FolderScreenSpace;

constexpr const char* kFontPath = "font/fontZurichBold";
constexpr const char* kStringsPath = "obseg/text/LtitleE";
constexpr const char* kBackdropPath = "eyeintro/sight";

// The view the folders are drawn through (viSetFovY, viSetAspect,
// viSetZRange).
constexpr float kFovY = 60.0f;
constexpr float kAspect = 1.3333334f; // ASPECT_RATIO_SD
constexpr float kNear = 100.0f;
constexpr float kFar = 10000.0f;

// The backdrop's scroll: floor(viGetX() * -80.0f / 1280.0f).
constexpr int32_t kBackdropOffset = -28;
constexpr FolderColour kBackdropTop = { 0x14, 0x14, 0x14 };
constexpr FolderColour kBackdropBottom = { 0x32, 0x32, 0x32 };

// Text colours (0xRRGGBBAA).
constexpr uint32_t kYellow = 0xEBD879FF;
constexpr uint32_t kWhite = 0xFFFFFFFF;
// The fill behind the erase prompt and a highlighted choice.
constexpr uint32_t kHighlightFill = 0x32;

// LTITLE string slots.
constexpr size_t kStringAgent = 19;
constexpr size_t kStringErase = 23;
constexpr size_t kStringCancel = 24;
constexpr size_t kStringConfirm = 25;
constexpr size_t kStringMission = 26;
constexpr size_t kStringCopy = 27;
constexpr size_t kStringEraseIcon = 28;

// The global image table entries of s_mainfolderimages (IMG_COPY, IMG_DEL and
// IMG_SEL), with the sizes the table gives them.
struct TableImage {
    int32_t offset;
    int32_t width;
    int32_t height;
};
constexpr TableImage kCopyImage = { 0xFD8, 0x20, 0x1C };
constexpr TableImage kEraseImage = { 0xFE4, 0x20, 0x1C };
constexpr TableImage kSelectImage = { 0xFF0, 0x7A, 0x12 };

// Where the three icons sit, and the line the Copy and Erase captions share.
constexpr float kCopyIconX = 225.0f;
constexpr float kEraseIconX = 335.0f;
constexpr float kSelectIconX = 110.0f;
constexpr float kIconY = 285.0f;
constexpr int32_t kCopyCaptionX = 247;
constexpr int32_t kEraseCaptionX = 357;

constexpr int32_t kHighestDifficulty007 = 3;

// SaveProgress as the model's folder interface.
class ProgressSave final : public FileSelectSave {
  public:
    explicit ProgressSave(Save::SaveProgress& progress) : progress_(progress) {
    }
    int32_t CurrentFolder() const override {
        return progress_.CurrentFolder();
    }
    void SetCurrentFolder(int32_t folder) override {
        progress_.SetCurrentFolder(folder);
    }
    int32_t PreviousFolder() const override {
        return progress_.PreviousFolder();
    }
    void HighestCompleted(int32_t folder, int32_t* stage, int32_t* difficulty) const override {
        const Save::StageDifficulty highest = progress_.HighestStageDifficultyCompleted(folder);
        *stage = highest.level;
        *difficulty = highest.difficulty;
    }
    void DeleteFolder(int32_t folder) override {
        progress_.DeleteFolder(folder);
    }
    void CopyFolderToFirstFree(int32_t folder) override {
        progress_.CopyFolderToFirstFree(folder);
    }

  private:
    Save::SaveProgress& progress_;
};

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    FileSelectModel model;
    FileSelectLayout layout;

    std::shared_ptr<Font> font;
    std::shared_ptr<Frigate::Text> strings;
    std::shared_ptr<Fast::Texture> backdrop;

    // Read by address while the interpreter draws the frame.
    Vp_t viewport{};
    Mtx::FixedMtx view{};
    Mtx::FixedMtx projection{};
    std::array<std::optional<Model::JointDraw>, WalletBond::kCount> draws;
    std::array<ScreenRect, WalletBond::kCount> folderBounds{};
};

State& GetState() {
    static State state;
    return state;
}

const std::string& StringAt(const State& state, size_t slot) {
    static const std::string kEmpty;
    return slot < state.strings->Strings.size() ? state.strings->Strings[slot] : kEmpty;
}

// The wallet's matrix: its position and scale, then the lookat that puts it
// 4000 units from the camera (interface_menu05_fileselect).
Mat4 WalletMatrix(int32_t folder) {
    Mat4 matrix = IdentityMat4();
    const Space::Point3 position = Space::CameraSpacePosition(folder);
    matrix.m[0][0] = Space::kFolderScale;
    matrix.m[1][1] = Space::kFolderScale;
    matrix.m[2][2] = Space::kFolderScale;
    matrix.m[3][0] = position[0];
    matrix.m[3][1] = position[1];
    matrix.m[3][2] = position[2];
    return matrix;
}

// modelGetXYExtents and projectRectCornersTo2D: the screen box of a posed
// wallet. The decomp passes the extents as {min, max} pairs.
ScreenRect ProjectBounds(const Model::ModelInstance& model, int32_t folder) {
    float xMax = 0.0f;
    float xMin = 0.0f;
    float yMax = 0.0f;
    float yMin = 0.0f;
    Level::AxisExtents(model, 0, &xMax, &xMin);
    Level::AxisExtents(model, 1, &yMax, &yMin);

    const Space::Point3 centre = Space::CameraSpacePosition(folder);
    ScreenRect rect;
    rect.right = Space::ProjectToScreen({ xMin, centre[1], centre[2] }).x;
    rect.left = Space::ProjectToScreen({ xMax, centre[1], centre[2] }).x;
    rect.down = Space::ProjectToScreen({ centre[0], yMax, centre[2] }).y;
    rect.up = Space::ProjectToScreen({ centre[0], yMin, centre[2] }).y;
    return rect;
}

// init_menu05_fileselect().
void Init(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;
    ProgressSave save(*front.progress);

    front.prevKeypresses = false;
    state.model.Init(save);
    front.tabNextSelected = false;
    front.tabPrevSelected = false;

    state.font = state.resourceManager->LoadResource<Font>(kFontPath);
    state.strings = state.resourceManager->LoadResource<Frigate::Text>(kStringsPath);
    state.backdrop = state.resourceManager->LoadResource<Fast::Texture>(kBackdropPath);
    if (state.font == nullptr || state.strings == nullptr || state.backdrop == nullptr) {
        spdlog::error("file select: the font, text or backdrop is missing from the loaded archives");
    }
    if (!front.walletBond->Load()) {
        spdlog::error("file select: the wallet model is missing from the loaded archives");
    }

    if (front.maybeIsInMenu) {
        // TODO(https://github.com/liz-baker/frigate/issues/242): musicTrack1Play(M_FOLDERS).
        front.maybeIsInMenu = false;
    }
    menu.ResetMenuTimer();
    for (size_t cheat = 1; cheat < kCheatInvalid; cheat++) {
        front.cheatActivated[cheat] = false;
    }
    front.appendCheatSinglePlayer = false;
    front.appendCheatMultiPlayer = false;

    state.viewport = MenuViewport();
    Gu::MatrixF matrix;
    Gu::TranslateF(matrix, 0.0f, 0.0f, 0.0f);
    state.view = Mtx::ToFixedMtx(matrix);
    Gu::PerspectiveF(matrix, kFovY, kAspect, kNear, kFar, 1.0f);
    state.projection = Mtx::ToFixedMtx(matrix);
}

// update_menu05_filesel(): the wallets are let go on the way to the legal
// screen.
void Update(const MenuStateMachine& menu) {
    State& state = GetState();
    if (menu.IsSwitchingTo(MenuId::LegalScreen)) {
        state.front->walletBond->CleanUp();
        for (auto& draw : state.draws) {
            draw.reset();
        }
        state.font.reset();
        state.strings.reset();
        state.backdrop.reset();
    }
}

// The per-wallet setup interface_menu05_fileselect() runs each frame: the
// render matrix, which pictures and tabs show, and the box the cursor is
// tested against.
void PoseWallets(State& state) {
    WalletBond& wallets = *state.front->walletBond;
    for (int32_t folder = 0; folder < WalletBond::kCount; folder++) {
        state.folderBounds[static_cast<size_t>(folder)] = {};
        if (!wallets.Loaded()) {
            continue;
        }
        Model::ModelInstance& model = *wallets.Instance(folder);
        DisableAllSwitches(model);
        SelectBondPicture(model, state.front->save->GetBondForFolder(static_cast<uint32_t>(folder)));
        model.SetSwitchVisible(WalletSwitch::kPhotoCover, true);
        model.SetSwitchVisible(WalletSwitch::kCover, true);
        model.Pose(WalletMatrix(folder));
        state.folderBounds[static_cast<size_t>(folder)] = ProjectBounds(model, folder);
    }
}

// interface_menu05_fileselect().
void Interface(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;

    const Input::Sample input = Input::ReadPlayer1();
    if (FileSelectModel::ResetsIdleTimer(input)) {
        menu.ResetMenuTimer();
    } else {
        menu.AddMenuTimer(menu.ClockTimer());
    }

    PoseWallets(state);
    state.layout.folderBounds = [&state](int32_t folder) { return state.folderBounds[static_cast<size_t>(folder)]; };

    ProgressSave save(*front.progress);
    const FileSelectModel::Next next = state.model.Update(input, menu.MenuTimer(), front.cursor, state.layout, save);
    // TODO(https://github.com/liz-baker/frigate/issues/242): play state.model.Sfx() (sndPlaySfx).

    switch (next) {
        case FileSelectModel::Next::ModeSelect:
            menu.ChangeMenu(MenuId::ModeSelect, /*reload=*/false);
            break;
        case FileSelectModel::Next::LegalScreen:
            menu.ChangeMenu(MenuId::LegalScreen, /*reload=*/true);
            break;
        case FileSelectModel::Next::Stay:
            break;
    }
}

// get_ptr_difficulty_name: the LTITLE slot of a difficulty's name, or none.
std::optional<size_t> DifficultyNameSlot(int32_t difficulty) {
    if (difficulty < 0 || difficulty > kHighestDifficulty007) {
        return std::nullopt;
    }
    return kStringAgent + static_cast<size_t>(difficulty);
}

Gfx* DrawText(Gfx* dl, const State& state, int32_t x, int32_t y, const std::string& text, uint32_t colour) {
    return Text::Render(dl, &x, &y, text, *state.font, colour, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
}

// A highlighted choice: the text on a dark box.
Gfx* DrawHighlighted(Gfx* dl, const State& state, int32_t x, int32_t y, const std::string& text) {
    const Text::Extent extent = Text::Measure(*state.font, text);
    dl = Text::FillRect(dl, x - 1, y - 1, extent.width + x + 3, extent.height + y, kHighlightFill);
    return DrawText(dl, state, x, y, text, kWhite);
}

// The erase prompt over a folder.
Gfx* DrawErasePrompt(Gfx* dl, const State& state, const Space::Point2& folder) {
    const auto fx = static_cast<int32_t>(std::floor(folder.x));
    const auto fy = static_cast<int32_t>(std::floor(folder.y));
    const bool cancelChosen = state.model.DeletionChoice() != 0;

    int32_t boxLeft = fx - 0x31;
    int32_t boxTop = fy + 0x19;
    dl = Text::FillRect(dl, boxLeft, boxTop, boxLeft + 0x63, boxTop + 0x2A, kHighlightFill);

    dl = DrawText(dl, state, fx - 0x2F, fy + 0x1E, StringAt(state, kStringErase), kYellow);

    const std::string& cancel = StringAt(state, kStringCancel);
    if (cancelChosen) {
        dl = DrawHighlighted(dl, state, fx - 0x2F, fy + 0x32, cancel);
    } else {
        dl = DrawText(dl, state, fx - 0x2F, fy + 0x32, cancel, kYellow);
    }

    // The Japanese version nudges the confirm text right (j_text_trigger).
    const std::string& confirm = StringAt(state, kStringConfirm);
    if (cancelChosen) {
        dl = DrawText(dl, state, -1 + fx - 1, fy + 0x32, confirm, kYellow);
    } else {
        dl = DrawHighlighted(dl, state, -1 + fx - 1, fy + 0x32, confirm);
    }
    return dl;
}

// What a folder shows when it has progress: its highest difficulty and the
// mission reached.
Gfx* DrawFolderProgress(Gfx* dl, const State& state, int32_t folder, const Space::Point2& position) {
    const Save::StageDifficulty highest = state.front->progress->HighestStageDifficultyCompleted(folder);
    const int32_t stage = highest.level;
    const int32_t difficulty = highest.difficulty;
    if (!(stage >= Save::kLevelDam && difficulty >= Save::kDifficultyAgent)) {
        return dl;
    }

    const auto fx = static_cast<int32_t>(std::floor(position.x));
    const auto fy = static_cast<int32_t>(std::floor(position.y));

    if (const auto slot = DifficultyNameSlot(difficulty)) {
        const std::string text = StringAt(state, *slot) + "\n";
        const Text::Extent extent = Text::Measure(*state.font, text);
        dl = DrawText(dl, state, (fx - (extent.width / 2)) - 1, fy + 0x15, text, kYellow);
    }

    if (difficulty != kHighestDifficulty007) {
        const std::span<const MissionFolderSetup> entries = MissionFolderEntries();
        int32_t chapter = -1;
        size_t part = 0;
        while (entries[part].folderTextSlot != 0) {
            if (stage == entries[part].missionNum) {
                chapter = ChapterBriefingEntry(static_cast<int32_t>(part));
                break;
            }
            part++;
        }

        std::string text = StringAt(state, kStringMission);
        if (chapter >= 0) {
            text += entries[static_cast<size_t>(chapter)].label;
            text += ".";
        }
        if (entries[part].label != nullptr) {
            text += entries[part].label;
        }
        text += "\n";
        const Text::Extent extent = Text::Measure(*state.font, text);
        dl = DrawText(dl, state, (fx - (extent.width / 2)) - 1, fy + 0x2D, text, kYellow);
    }
    return dl;
}

// An icon of the global image table, drawn at its centre with its half size.
Gfx* DrawTableImage(Gfx* dl, const TableImage& image, float x, float y) {
    const float position[2] = { x, y };
    const float half[2] = { static_cast<float>(image.width) * 0.5f, static_cast<float>(image.height) * 0.5f };
    dl = Level::SelectImage(dl, image.offset, 4);
    return Level::DisplayImageAtPosition(dl, position, half, image.width, image.height, false, false, true, 0xFF, 0xFF,
                                         0xFF, 0xFF);
}

// The Copy and Erase captions and icons, which also set the boxes the cursor
// is tested against, and the "select file" title.
Gfx* DrawOptions(Gfx* dl, State& state) {
    const std::string& copy = StringAt(state, kStringCopy);
    const Text::Extent copySize = Text::Measure(*state.font, copy);
    int32_t x = kCopyCaptionX;
    int32_t y = 285 - (copySize.height / 2);
    dl = Text::Render(dl, &x, &y, copy, *state.font, kWhite, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
    state.layout.copy.right = static_cast<float>(copySize.width + x);

    const std::string& erase = StringAt(state, kStringEraseIcon);
    const Text::Extent eraseSize = Text::Measure(*state.font, erase);
    x = kEraseCaptionX;
    y = 285 - (eraseSize.height / 2);
    dl = Text::Render(dl, &x, &y, erase, *state.font, kWhite, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
    state.layout.erase.right = static_cast<float>(eraseSize.width + x);

    dl = DrawTableImage(dl, kCopyImage, kCopyIconX, kIconY);
    state.layout.copy.left = kCopyIconX - (static_cast<float>(kCopyImage.width) * 0.5f);
    state.layout.copy.up = kIconY - (static_cast<float>(kCopyImage.height) * 0.5f);
    state.layout.copy.down = kIconY + (static_cast<float>(kCopyImage.height) * 0.5f);

    dl = DrawTableImage(dl, kEraseImage, kEraseIconX, kIconY);
    state.layout.erase.left = kEraseIconX - (static_cast<float>(kEraseImage.width) * 0.5f);
    state.layout.erase.up = kIconY - (static_cast<float>(kEraseImage.height) * 0.5f);
    state.layout.erase.down = kIconY + (static_cast<float>(kEraseImage.height) * 0.5f);

    return DrawTableImage(dl, kSelectImage, kSelectIconX, kIconY);
}

// constructor_menu05_fileselect().
Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    using namespace Frigate::F3dex;
    if (state.font == nullptr || state.strings == nullptr || state.backdrop == nullptr) {
        return dl;
    }

    gDPSetCombineLERP(dl++, TEXEL0, PRIMITIVE, ENV_ALPHA, PRIMITIVE, 0, 0, 0, PRIMITIVE, TEXEL0, PRIMITIVE, ENV_ALPHA,
                      PRIMITIVE, 0, 0, 0, PRIMITIVE);
    gDPSetEnvColor(dl++, 0xFF, 0xFF, 0xFF, 0x14);
    dl = DrawFolderBackground(dl, *state.backdrop, kBackdropOffset, kBackdropTop, kBackdropBottom);

    dl = BasicGeometry(dl);
    dl = FastPipelineSetup(dl);

    // viSetupCurrentPlayerView(): the viewport and projection the wallets
    // are drawn through. The modelview loads first: the interpreter multiplies
    // a projection load by the top of the modelview stack, which is empty
    // until the first modelview load.
    *dl++ = Viewport(&state.viewport);
    *dl++ = Matrix(&state.view, Fast::F3DEX_G_MTX_MODELVIEW | Fast::F3DEX_G_MTX_LOAD);
    *dl++ = Matrix(&state.projection, Fast::F3DEX_G_MTX_PROJECTION | Fast::F3DEX_G_MTX_LOAD);

    // unknown_folderselect: no z-buffer, the primary and secondary lists node
    // by node.
    ModelRenderParams params;
    params.zbufferEnabled = false;
    params.passes = 3;
    params.sortJoints = false;
    WalletBond& wallets = *state.front->walletBond;
    for (int32_t folder = WalletBond::kCount - 1; folder >= 0; folder--) {
        auto& draw = state.draws[static_cast<size_t>(folder)];
        draw.reset();
        if (!wallets.Loaded()) {
            continue;
        }
        draw = Model::BuildJointDraw(state.resourceManager, { wallets.Instance(folder) }, params);
        if (draw) {
            for (const Gfx& command : draw->commands) {
                *dl++ = command;
            }
        }
    }

    dl = Text::SetupRdp(dl);

    for (int32_t folder = 0; folder < WalletBond::kCount; folder++) {
        const Space::Point2 position = Space::ProjectToScreen(Space::CameraSpacePosition(folder));
        if (folder == state.model.FolderSelectedForDeletion()) {
            dl = DrawErasePrompt(dl, state, position);
        } else {
            dl = DrawFolderProgress(dl, state, folder, position);
        }
    }

    dl = DrawOptions(dl, state);

    if (state.model.FolderSelectedForDeletion() < 0) {
        dl = DrawFrontCursor(dl, state.front->cursor, state.model.OptionIcon());
    }
    return dl;
}

} // namespace

void RegisterFileSelect(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                        const std::shared_ptr<FrontState>& front) {
    State& state = GetState();
    state.front = front;
    state.resourceManager = resourceManager;
    if (front->walletBond == nullptr) {
        front->walletBond = std::make_shared<WalletBond>(resourceManager);
    }
    menu.RegisterMenu(MenuId::FileSelect, {
                                              .onUpdate = Update,
                                              .onInit = Init,
                                              .onInterface = Interface,
                                              .onConstruct = Construct,
                                          });
}

} // namespace Frigate::Game::Menu
