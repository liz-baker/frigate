#include "game/menu/MissionSelect.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "fast/resource/type/Vertex.h"
#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "game/menu/FolderTabs.h"
#include "game/menu/FrontCursor.h"
#include "game/menu/MissionFolders.h"
#include "game/menu/MissionSelectModel.h"
#include "game/menu/SaveMissionProgress.h"
#include "game/menu/WalletBackground.h"
#include "game/menu/WalletBond.h"
#include "game/save/SaveProgress.h"
#include "gfx/GbiF3dex.h"
#include "model/ModelInstance.h"
#include "resource/type/Font.h"
#include "resource/type/Text.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

constexpr const char* kFontPath = "font/fontBankGothic";
constexpr const char* kStringsPath = "obseg/text/LtitleE";

// LTITLE string 6, the Previous tab's label.
constexpr size_t kStringPrevious = 6;

// Each mission's icon is four vertices of the icons' list.
constexpr int32_t kVerticesPerIcon = 4;

// The icon text's colours (0xRRGGBB00): unlocked, and the one under the cursor.
constexpr uint32_t kIconColour = 0x96969600;
constexpr uint32_t kHighlightedIconColour = 0xFFFFFF00;

// The offsets of an icon's caption from its column and row.
constexpr int32_t kCaptionLeft = 0x1F;
constexpr int32_t kCaptionBottom = 0x1D;

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    MissionSelectModel model;

    std::shared_ptr<Font> font;
    std::shared_ptr<Frigate::Text> strings;

    // The node whose vertices colour the icons, and how many it has.
    uint32_t iconsNode = 0;
    int32_t iconVertexCount = 0;

    std::optional<WalletBackground> background;
};

State& GetState() {
    static State state;
    return state;
}

const std::string& StringAt(const State& state, size_t slot) {
    static const std::string kEmpty;
    return slot < state.strings->Strings.size() ? state.strings->Strings[slot] : kEmpty;
}

// The vertex count of the icons' list, read once the wallet is loaded.
void FindIconVertices(State& state) {
    state.iconsNode = 0;
    state.iconVertexCount = 0;
    WalletBond& wallets = *state.front->walletBond;
    if (!wallets.Loaded()) {
        return;
    }
    const Model::ModelInstance& model = *wallets.Instance(0);
    const ModelNode* node = model.SwitchNode(WalletSwitch::kPicsNode);
    if (node == nullptr) {
        spdlog::error("mission select: the wallet model has no icon node");
        return;
    }
    const std::string path = fmt::format("{}/vtx/{:06x}", model.Path(), node->offset);
    auto vertices = state.resourceManager->LoadResource<Fast::Vertex>(path);
    if (vertices == nullptr) {
        spdlog::error("mission select: '{}' does not load", path);
        return;
    }
    state.iconsNode = node->offset;
    state.iconVertexCount = static_cast<int32_t>(vertices->VertexList.size());
}

// init_menu07_missionselect().
void Init(MenuStateMachine&) {
    State& state = GetState();
    FrontState& front = *state.front;

    state.model.Init();
    front.selectedStage = state.model.SelectedStage();
    front.briefingPage = state.model.BriefingPage();
    front.tabNextSelected = false;
    front.tabPrevSelected = false;
    if (front.maybeIsInMenu) {
        // TODO(https://github.com/liz-baker/frigate/issues/242): musicTrack1Play(M_FOLDERS).
        front.maybeIsInMenu = false;
    }

    state.font = state.resourceManager->LoadResource<Font>(kFontPath);
    state.strings = state.resourceManager->LoadResource<Frigate::Text>(kStringsPath);
    if (state.font == nullptr || state.strings == nullptr) {
        spdlog::error("mission select: the font or text is missing from the loaded archives");
    }
    if (!front.walletBond->Load()) {
        spdlog::error("mission select: the wallet model is missing from the loaded archives");
    }
    FindIconVertices(state);

    state.background.emplace(state.resourceManager);
    state.background->Init();
}

// The wallet's setup interface_menu07_missionsel() runs each frame: which
// parts show.
void SetWalletSwitches(State& state) {
    WalletBond& wallets = *state.front->walletBond;
    if (!wallets.Loaded()) {
        return;
    }
    Model::ModelInstance& model = *wallets.Instance(0);
    DisableAllSwitches(model);
    model.SetSwitchVisible(WalletSwitch::kTabs, true);
    model.SetSwitchVisible(WalletSwitch::kSlides, true);
    model.SetSwitchVisible(WalletSwitch::kPics, true);
}

void PaintIcons(State& state, const MissionSelectProgress& progress) {
    Model::ModelInstance& model = *state.front->walletBond->Instance(0);
    model.ClearPaints();
    for (int32_t i = 0; i < state.iconVertexCount; i++) {
        const IconColour colour =
            MissionSelectModel::IconVertexColour(i / kVerticesPerIcon, state.model.Highlighted(), progress);
        model.PaintVertex({ .head = false,
                            .node = state.iconsNode,
                            .vertex = i,
                            .alpha = colour.alpha,
                            .colour = std::array<uint8_t, 3>{ colour.red, colour.green, colour.blue } });
    }
}

// interface_menu07_missionsel().
void Interface(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;

    const Input::Sample input = Input::ReadPlayer1();
    const SaveMissionProgress progress(*front.progress, front.appendCheatSinglePlayer);
    const MissionSelectModel::Next next = state.model.Update(input, front.cursor, progress);
    // TODO(https://github.com/liz-baker/frigate/issues/242): play state.model.Sfx() (sndPlaySfx).

    front.selectedStage = state.model.SelectedStage();
    front.briefingPage = state.model.BriefingPage();
    front.tabNextSelected = state.model.TabNextSelected();
    front.tabPrevSelected = state.model.TabPrevSelected();

    SetWalletSwitches(state);
    if (state.front->walletBond->Loaded() && state.iconVertexCount > 0) {
        PaintIcons(state, progress);
    }

    switch (next) {
        case MissionSelectModel::Next::Difficulty:
            // TODO(https://github.com/liz-baker/frigate/issues/293): the difficulty screen's handlers; until then
            // the screen it lands on draws only the black clear.
            menu.ChangeMenu(MenuId::Difficulty, /*reload=*/false);
            break;
        case MissionSelectModel::Next::ModeSelect:
            menu.ChangeMenu(MenuId::ModeSelect, /*reload=*/false);
            break;
        case MissionSelectModel::Next::Stay:
            break;
    }
}

// The caption under a mission's icon, in capitals outside the Japanese
// version.
std::string IconCaption(const State& state, const MissionFolderSetup& entry) {
    std::string text =
        StringAt(state, static_cast<size_t>(entry.iconTextSlot != 0 ? entry.iconTextSlot : entry.folderTextSlot));
    for (char& c : text) {
        if (static_cast<unsigned char>(c) >= 'a') {
            c = static_cast<char>(static_cast<unsigned char>(c) - 0x20);
        }
    }
    text += "\n";
    return text;
}

Gfx* DrawCaptions(Gfx* dl, const State& state, const MissionSelectProgress& progress) {
    for (int32_t column = 0; column < MissionSelectModel::kColumns; column++) {
        for (int32_t row = 0; row < MissionSelectModel::kRows; row++) {
            const int32_t level = column + (row * MissionSelectModel::kColumns);
            const int32_t entryIndex = MissionSelectModel::EntryForLevel(level);
            if (entryIndex < 0 || MissionSelectModel::HighestUnlockedDifficulty(level, progress) < 0) {
                continue;
            }

            const uint32_t colour = level == state.model.Highlighted() ? kHighlightedIconColour : kIconColour;
            const std::string text = IconCaption(state, MissionFolderEntries()[static_cast<size_t>(entryIndex)]);
            const Text::Extent extent = Text::Measure(*state.font, text);

            int32_t x = MissionSelectModel::kColumnX[static_cast<size_t>(column)] - kCaptionLeft;
            int32_t y = (MissionSelectModel::kRowY[static_cast<size_t>(row)] - extent.height) + kCaptionBottom;
            dl = Text::FillRect(dl, x, y, x + extent.width, y + extent.height, 0);
            dl = Text::Render(dl, &x, &y, text, *state.font, colour | 0xFF, kMenuScreenWidth, kMenuScreenHeight, 0, 0);

            x = MissionSelectModel::kColumnX[static_cast<size_t>(column)] - kCaptionLeft;
            y = (MissionSelectModel::kRowY[static_cast<size_t>(row)] - extent.height) + kCaptionBottom;
            dl = Text::Render(dl, &x, &y, text, *state.font, colour | 0x64, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
        }
    }
    return dl;
}

// constructor_menu07_missionsel().
Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    using namespace Frigate::F3dex;
    if (state.font == nullptr || state.strings == nullptr) {
        return dl;
    }

    gDPSetFogColor(dl++, 0xFF, 0xFF, 0xFF, 0xFF);

    dl = state.background->Draw(dl, *state.front->walletBond, state.front->progress->CurrentFolder());

    dl = Text::SetupRdp(dl);
    const SaveMissionProgress progress(*state.front->progress, state.front->appendCheatSinglePlayer);
    dl = DrawCaptions(dl, state, progress);

    dl = DrawPreviousTab(dl, *state.font, StringAt(state, kStringPrevious), state.model.PreviousTabHighlighted());
    return DrawFrontCursor(dl, state.front->cursor, 0);
}

} // namespace

void RegisterMissionSelect(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                           const std::shared_ptr<FrontState>& front) {
    State& state = GetState();
    state.front = front;
    state.resourceManager = resourceManager;
    if (front->walletBond == nullptr) {
        front->walletBond = std::make_shared<WalletBond>(resourceManager);
    }
    // update_menu07_missionsel does nothing.
    menu.RegisterMenu(MenuId::MissionSelect, {
                                                 .onInit = Init,
                                                 .onInterface = Interface,
                                                 .onConstruct = Construct,
                                             });
}

} // namespace Frigate::Game::Menu
