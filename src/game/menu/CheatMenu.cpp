#include "game/menu/CheatMenu.h"

#include <cstdint>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "game/menu/CheatMenuModel.h"
#include "game/menu/CheatTable.h"
#include "game/menu/FolderTabs.h"
#include "game/menu/FrontCursor.h"
#include "game/menu/WalletBackground.h"
#include "game/menu/WalletBond.h"
#include "game/save/SaveProgress.h"
#include "level/Lang.h"
#include "resource/type/Font.h"
#include "resource/type/Text.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

constexpr const char* kFontPath = "font/fontZurichBold";
constexpr const char* kGothicFontPath = "font/fontBankGothic";

// LTITLE slots.
constexpr int32_t kTitlePrevious = 6;
constexpr int32_t kTitleOn = 115;
constexpr int32_t kTitleOff = 116;
// MISC_STR_28_NONAME.
constexpr int32_t kMiscNoName = 0x28;

// Where the two columns' names and on/off texts sit, and the first row.
constexpr int32_t kLeftNameX = 0x37;
constexpr int32_t kLeftStateX = 0xB3;
constexpr int32_t kRightNameX = 0xDC;
constexpr int32_t kRightStateX = 0x158;

// The dark box behind the highlighted name, and its margins.
constexpr uint32_t kHighlightFill = 0x32;

// Text colours (0xRRGGBBAA): black names, a dark red for a cheat that is on.
constexpr uint32_t kBlack = 0xFF;
constexpr uint32_t kOnColour = 0xA00000FF;

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    CheatMenuModel model;
    std::optional<WalletBackground> background;

    std::shared_ptr<Font> font;
    std::shared_ptr<Font> gothic;
    Level::Lang lang;
    bool loaded = false;
};

State& GetState() {
    static State state;
    return state;
}

// Loads a text bank into `lang`; false if it is missing.
bool LoadBank(State& state, int32_t bank) {
    const auto strings = state.resourceManager->LoadResource<Frigate::Text>(Level::Lang::BankResource(bank));
    if (strings == nullptr) {
        return false;
    }
    state.lang.SetBank(bank, strings->Strings);
    return true;
}

// init_menu15_cheat().
void Init(MenuStateMachine&) {
    State& state = GetState();
    FrontState& front = *state.front;

    state.model.Init(front);

    state.font = state.resourceManager->LoadResource<Font>(kFontPath);
    state.gothic = state.resourceManager->LoadResource<Font>(kGothicFontPath);
    state.loaded = state.font != nullptr && state.gothic != nullptr && LoadBank(state, Level::TextBank::Title) &&
                   LoadBank(state, Level::TextBank::Misc);
    if (!state.loaded) {
        spdlog::error("cheat menu: a font or text bank is missing from the loaded archives");
    }
    if (!front.walletBond->Load()) {
        spdlog::error("cheat menu: the wallet model is missing from the loaded archives");
    }
    state.background.emplace(state.resourceManager);
    state.background->Init();
}

// update_menu15_cheat(): runs as the menu is left. The loaded fonts and text
// go with it.
void Update(const MenuStateMachine&) {
    State& state = GetState();
    CheatMenuModel::UpdateAppendFlags(*state.front);
    state.background->Release();
    state.font.reset();
    state.gothic.reset();
    state.loaded = false;
}

// interface_menu15_cheat().
void Interface(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;

    const Input::Sample input = Input::ReadPlayer1();

    if (front.walletBond->Loaded()) {
        Model::ModelInstance& wallet = *front.walletBond->Instance(0);
        DisableAllSwitches(wallet);
        wallet.SetSwitchVisible(WalletSwitch::kTabs, true);
        wallet.SetSwitchVisible(WalletSwitch::kBlank, true);
        wallet.SetSwitchVisible(WalletSwitch::kClassified, true);
    }

    const CheatMenuModel::Next next = state.model.Update(input, front);
    // TODO(https://github.com/liz-baker/frigate/issues/242): play state.model.Sfx() (sndPlaySfx).

    if (next == CheatMenuModel::Next::ModeSelect) {
        menu.ChangeMenu(MenuId::ModeSelect, /*reload=*/false);
    }
}

// cheatGetMenuTextPointer.
std::string_view CheatName(const State& state, int32_t cheat) {
    const std::optional<int32_t> slot = Cheat::MenuTextSlot(cheat);
    return state.lang.Get(Level::StringId(Level::TextBank::Misc, slot.value_or(kMiscNoName)));
}

// One cheat's row: its name, highlighted when the cursor is on it, and its
// on or off state.
Gfx* DrawRow(Gfx* dl, const State& state, int32_t index, int32_t row, int32_t nameX, int32_t stateX) {
    const FrontState& front = *state.front;
    const int32_t cheat = state.model.UnlockedCheat(index);
    const bool on = front.cheatActivated[static_cast<size_t>(cheat)];
    const int32_t y = (row * CheatMenuModel::kRowHeight) + CheatMenuModel::kRowTop;

    const std::string_view name = CheatName(state, cheat);
    const Text::Extent nameSize = Text::Measure(*state.font, name);
    if (index == state.model.Highlighted() && !CursorOnPreviousTab(front.cursor)) {
        dl = Text::FillRect(dl, nameX - 2, y - 1, nameX + nameSize.width + 5, y + 0xE, kHighlightFill);
    }
    int32_t x = nameX;
    int32_t textY = y;
    dl = Text::Render(dl, &x, &textY, name, *state.font, kBlack, kMenuScreenWidth, kMenuScreenHeight, 0, 0);

    const std::string_view status = state.lang.Get(Level::StringId(Level::TextBank::Title, on ? kTitleOn : kTitleOff));
    x = stateX;
    textY = y;
    return Text::Render(dl, &x, &textY, status, *state.font, on ? kOnColour : kBlack, kMenuScreenWidth,
                        kMenuScreenHeight, 0, 0);
}

// constructor_menu15_cheat().
Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    FrontState& front = *state.front;
    if (!state.loaded) {
        return dl;
    }

    dl = state.background->Draw(dl, *front.walletBond, front.progress->CurrentFolder());
    dl = Text::SetupRdp(dl);

    const int32_t total = state.model.TotalUnlocked();
    const int32_t leftRows = total > CheatMenuModel::kColumnRows ? CheatMenuModel::kColumnRows : total;
    for (int32_t row = 0; row < leftRows; row++) {
        dl = DrawRow(dl, state, row, row, kLeftNameX, kLeftStateX);
    }
    if (total > CheatMenuModel::kColumnRows) {
        for (int32_t row = 0; row < total - CheatMenuModel::kColumnRows; row++) {
            dl = DrawRow(dl, state, row + CheatMenuModel::kColumnRows, row, kRightNameX, kRightStateX);
        }
    }

    const std::string_view previous = state.lang.Get(Level::StringId(Level::TextBank::Title, kTitlePrevious));
    dl = DrawPreviousTab(dl, *state.gothic, previous, state.model.PreviousTabHighlighted());

    return DrawFrontCursor(dl, front.cursor, 0);
}

} // namespace

void RegisterCheatMenu(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                       const std::shared_ptr<FrontState>& front) {
    State& state = GetState();
    state.front = front;
    state.resourceManager = resourceManager;
    if (front->walletBond == nullptr) {
        front->walletBond = std::make_shared<WalletBond>(resourceManager);
    }
    menu.RegisterMenu(MenuId::Cheat, {
                                         .onUpdate = Update,
                                         .onInit = Init,
                                         .onInterface = Interface,
                                         .onConstruct = Construct,
                                     });
}

} // namespace Frigate::Game::Menu
