#include "game/menu/ModeSelect.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "game/menu/CheatAvailability.h"
#include "game/menu/FolderTabs.h"
#include "game/menu/FrontCursor.h"
#include "game/menu/MenuAudio.h"
#include "game/menu/ModeSelectModel.h"
#include "game/menu/TitleStrings.h"
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

constexpr const char* kFontPath = "font/fontZurichBold";
constexpr const char* kTabFontPath = "font/fontBankGothic";
constexpr const char* kStringsPath = "obseg/text/LtitleE";

// LTITLE string slots.
constexpr size_t kStringSelectMission = 29;
constexpr size_t kStringMultiplayer = 30;
constexpr size_t kStringCheatOptions = 31;
constexpr size_t kStringCheatSelectMission = 117;
constexpr size_t kStringCheatMultiplayer = 276;

// The three entries' rows: the number, the label and the highlight box.
struct EntryLayout {
    int32_t y;
    int32_t boxTop;
    int32_t boxBottom;
};
constexpr int32_t kNumberX = 0x96;
constexpr int32_t kLabelX = 0xAA;
constexpr int32_t kBoxLeft = 0x94;
constexpr int32_t kBoxWidthPad = 0xAF;
constexpr EntryLayout kSinglePlayerRow = { 0xDC, 0xDA, 0xEA };
constexpr EntryLayout kMultiplayerRow = { 0xFC, 0xFA, 0x10A };
constexpr EntryLayout kCheatsRow = { 0x11C, 0x11A, 0x12A };

constexpr uint32_t kBlack = 0x000000FF;
// The multiplayer entry's text with fewer than two controllers (alpha 0x70).
constexpr uint32_t kDimmed = 0x00000070;
constexpr uint32_t kBoxFill = 0x32;

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    ModeSelectModel model;

    std::shared_ptr<Font> font;
    std::shared_ptr<Font> tabFont;
    std::shared_ptr<Frigate::Text> strings;

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

// init_menu06_modeselect().
void Init(MenuStateMachine&) {
    State& state = GetState();
    FrontState& front = *state.front;

    state.model.Init(front);
    front.progress->UpdateBondInCurrentFolder();
    if (state.resourceManager == nullptr) {
        return;
    }

    if (!front.walletBond->Load()) {
        spdlog::error("mode select: the wallet model is missing from the loaded archives");
    }
    state.font = state.resourceManager->LoadResource<Font>(kFontPath);
    state.tabFont = state.resourceManager->LoadResource<Font>(kTabFontPath);
    state.strings = state.resourceManager->LoadResource<Frigate::Text>(kStringsPath);
    if (state.font == nullptr || state.tabFont == nullptr || state.strings == nullptr) {
        spdlog::error("mode select: a font or the text is missing from the loaded archives");
    }

    state.background.emplace(state.resourceManager);
    state.background->Init();
}

// The switches interface_menu06_modesel() shows on the wallet.
void SetWalletSwitches(State& state) {
    WalletBond& wallets = *state.front->walletBond;
    if (!wallets.Loaded()) {
        return;
    }
    const int32_t folder = state.front->progress->CurrentFolder();
    Model::ModelInstance& model = *wallets.Instance(0);
    DisableAllSwitches(model);
    SelectBondPicture(model, state.front->save->GetBondForFolder(static_cast<uint32_t>(folder)));
    model.SetSwitchVisible(WalletSwitch::kTabs, true);
    model.SetSwitchVisible(WalletSwitch::kPaper, true);
    model.SetSwitchVisible(WalletSwitch::kOhmss, true);
    model.SetSwitchVisible(WalletSwitch::kPhotoBond, true);
    model.SetSwitchVisible(WalletSwitch::kEyesOnly, true);
}

// interface_menu06_modesel().
void Interface(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;
    const Save::SaveProgress& progress = *front.progress;

    const Input::Sample input = Input::ReadPlayer1();
    const ModeSelectModel::Next next =
        state.model.Update(input, front, Input::ControllerCount(), [&progress](int32_t cheat) {
            return IsCheatAvailable(progress, progress.CurrentFolder(), cheat);
        });
    PlayCues(state.model.Sfx());

    SetWalletSwitches(state);

    switch (next) {
        case ModeSelectModel::Next::MissionSelect:
            menu.ChangeMenu(MenuId::MissionSelect, /*reload=*/false);
            break;
        case ModeSelectModel::Next::MultiplayerOptions:
            menu.ChangeMenu(MenuId::MultiplayerOptions, /*reload=*/false);
            break;
        case ModeSelectModel::Next::Cheat:
            menu.ChangeMenu(MenuId::Cheat, /*reload=*/false);
            break;
        case ModeSelectModel::Next::FileSelect:
            menu.ChangeMenu(MenuId::FileSelect, /*reload=*/false);
            break;
        case ModeSelectModel::Next::Stay:
            break;
    }
}

Gfx* DrawText(Gfx* dl, const State& state, int32_t x, int32_t y, const std::string& text, uint32_t colour) {
    return Text::Render(dl, &x, &y, text, *state.font, colour, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
}

// One entry: its number, its highlight box when chosen, and its label.
Gfx* DrawEntry(Gfx* dl, const State& state, const EntryLayout& row, const char* number, const std::string& label,
               bool highlighted, uint32_t colour) {
    dl = DrawText(dl, state, kNumberX, row.y, number, colour);
    if (highlighted) {
        const Text::Extent extent = Text::Measure(*state.font, label);
        dl = Text::FillRect(dl, kBoxLeft, row.boxTop, extent.width + kBoxWidthPad, row.boxBottom, kBoxFill);
    }
    return DrawText(dl, state, kLabelX, row.y, label, colour);
}

// constructor_menu06_modesel().
Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    if (state.font == nullptr || state.tabFont == nullptr || state.strings == nullptr) {
        return dl;
    }
    const FrontState& front = *state.front;

    dl = state.background->Draw(dl, *front.walletBond, front.progress->CurrentFolder());

    dl = Text::SetupRdp(dl);

    using Highlight = ModeSelectModel::Highlight;
    const Highlight highlight = state.model.CurrentHighlight();

    dl = DrawEntry(dl, state, kSinglePlayerRow, "1.\n",
                   StringAt(state, front.appendCheatSinglePlayer ? kStringCheatSelectMission : kStringSelectMission),
                   highlight == Highlight::SinglePlayer, kBlack);

    // The multiplayer entry is dimmed, and still chosen by the cursor, unless a
    // second controller is connected.
    const uint32_t multiplayerColour = Input::ControllerCount() >= 2 ? kBlack : kDimmed;
    dl = DrawEntry(dl, state, kMultiplayerRow, "2.\n",
                   StringAt(state, front.appendCheatMultiPlayer ? kStringCheatMultiplayer : kStringMultiplayer),
                   highlight == Highlight::Multiplayer, multiplayerColour);

    if (state.model.CheatMenuAvailable()) {
        dl = DrawEntry(dl, state, kCheatsRow, "3.\n", StringAt(state, kStringCheatOptions),
                       highlight == Highlight::Cheats, kBlack);
    }

    dl = DrawPreviousTab(dl, *state.tabFont, StringAt(state, TitleString::kPrevious),
                         state.model.PreviousTabHighlighted());
    return DrawFrontCursor(dl, front.cursor, 0);
}

} // namespace

void RegisterModeSelect(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                        const std::shared_ptr<FrontState>& front) {
    State& state = GetState();
    state.front = front;
    state.resourceManager = resourceManager;
    if (front->walletBond == nullptr) {
        front->walletBond = std::make_shared<WalletBond>(resourceManager);
    }
    menu.RegisterMenu(MenuId::ModeSelect, {
                                              .onInit = Init,
                                              .onInterface = Interface,
                                              .onConstruct = Construct,
                                          });
}

} // namespace Frigate::Game::Menu
