#include "game/menu/MpOptionsScreen.h"

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/MenuStateMachine.h"
#include "game/MultiplayerOptions.h"
#include "game/ScreenSize.h"
#include "game/TextRender.h"
#include "game/menu/FolderPaper.h"
#include "game/menu/FolderTabs.h"
#include "game/menu/FrontCursor.h"
#include "game/menu/MenuAudio.h"
#include "game/menu/MpOptionsModel.h"
#include "game/menu/TitleStrings.h"
#include "game/menu/WalletBond.h"
#include "level/Lang.h"
#include "model/Random.h"
#include "ship/resource/ResourceManager.h"

namespace Frigate::Game::Menu {

namespace {

constexpr uint32_t kBlack = 0x000000FF;
// A locked row's text (alpha 0x70).
constexpr uint32_t kDimmed = 0x00000070;
constexpr uint32_t kBoxFill = 0x32;

constexpr int32_t kTitleX = 0x37;
constexpr int32_t kTitleY = 0x5F;
constexpr int32_t kLabelX = 0x39;
constexpr int32_t kValueX = 0xA0;
constexpr int32_t kBoxLeft = 0x37;
constexpr int32_t kBoxWidthPad = 0x3C;

// The label rows' y; the highlight box runs from one above to fourteen below.
constexpr int32_t kPlayersY = 0x79;
constexpr int32_t kScenarioY = 0x8D;
constexpr int32_t kStageY = 0xA1;
constexpr int32_t kGameLengthY = 0xB5;
constexpr int32_t kWeaponsY = 0xC9;
constexpr int32_t kCharacterY = 0xDD;
constexpr int32_t kHealthY = 0xF1;
constexpr int32_t kControlStyleY = 0x105;
constexpr int32_t kAimY = 0x119;
constexpr int32_t kBoxTopOffset = -1;
constexpr int32_t kBoxBottomOffset = 14;

struct State {
    std::shared_ptr<FrontState> front;
    std::shared_ptr<Ship::ResourceManager> resourceManager;
    MpOptionsModel model;
    FolderPaper paper;
    std::shared_ptr<Frigate::Text> weaponStrings;
};

State& GetState() {
    static State state;
    return state;
}

// init_menu0E_mpoptions().
void Init(MenuStateMachine&) {
    State& state = GetState();
    FrontState& front = *state.front;

    state.model.Init(front, Input::ControllerCount());
    if (front.maybeIsInMenu) {
        Audio().PlayFolderMusicAtFullVolume();
        front.maybeIsInMenu = false;
    }
    if (state.resourceManager == nullptr) {
        return;
    }
    state.paper.Load(state.resourceManager, front);
    state.weaponStrings =
        state.resourceManager->LoadResource<Frigate::Text>(Level::Lang::BankResource(Level::TextBank::MpWeapons));
    if (state.weaponStrings == nullptr) {
        spdlog::error("multiplayer options: the weapon names are missing from the loaded archives");
    }
}

// interface_menu0E_mpoptions().
void Interface(MenuStateMachine& menu) {
    State& state = GetState();
    FrontState& front = *state.front;

    const auto& next = state.model.Update(Input::ReadPlayer1(), front, Input::ControllerCount(), RandomGetNext);
    PlayCues(state.model.Sfx());
    FolderPaper::ShowSwitches(
        front, { WalletSwitch::kTabs, WalletSwitch::kPaper, WalletSwitch::kOhmss, WalletSwitch::kConfidential2 });

    for (const MpOptionsModel::Next target : next) {
        switch (target) {
            case MpOptionsModel::Next::ModeSelect:
                menu.ChangeMenu(MenuId::ModeSelect, /*reload=*/false);
                break;
            case MpOptionsModel::Next::RunStage:
                menu.ChangeMenu(MenuId::RunStage, /*reload=*/true);
                break;
            case MpOptionsModel::Next::ScenarioSelect:
                // TODO(https://github.com/liz-baker/frigate/issues/299): the scenario select.
                menu.ChangeMenu(MenuId::MultiplayerScenarioSelect, /*reload=*/false);
                break;
            case MpOptionsModel::Next::StageSelect:
                // TODO(https://github.com/liz-baker/frigate/issues/299): the stage select.
                menu.ChangeMenu(MenuId::MultiplayerStageSelect, /*reload=*/false);
                break;
            case MpOptionsModel::Next::CharacterSelect:
                // TODO(https://github.com/liz-baker/frigate/issues/298): the character select.
                menu.ChangeMenu(MenuId::MultiplayerCharacterSelect, /*reload=*/false);
                break;
            case MpOptionsModel::Next::Handicap:
                // TODO(https://github.com/liz-baker/frigate/issues/300): the handicap screen.
                menu.ChangeMenu(MenuId::MultiplayerHandicap, /*reload=*/false);
                break;
            case MpOptionsModel::Next::ControlStyle:
                // TODO(https://github.com/liz-baker/frigate/issues/300): the control style screen.
                menu.ChangeMenu(MenuId::MultiplayerControlStyle, /*reload=*/false);
                break;
        }
    }
}

Gfx* DrawText(Gfx* dl, const State& state, int32_t x, int32_t y, const std::string& text, uint32_t colour) {
    return Text::Render(dl, &x, &y, text, state.paper.Zurich(), colour, kMenuScreenWidth, kMenuScreenHeight, 0, 0);
}

// A label at kLabelX on its row, in its highlight box when chosen and dimmed
// when the scenario locks it.
Gfx* DrawLabel(Gfx* dl, const State& state, int32_t y, size_t slot, bool highlighted, bool unlocked) {
    const std::string& text = state.paper.String(slot);
    if (highlighted) {
        const Text::Extent extent = Text::Measure(state.paper.Zurich(), text);
        dl = Text::FillRect(dl, kBoxLeft, y + kBoxTopOffset, extent.width + kBoxWidthPad, y + kBoxBottomOffset,
                            kBoxFill);
    }
    return DrawText(dl, state, kLabelX, y, text, unlocked ? kBlack : kDimmed);
}

Gfx* DrawValue(Gfx* dl, const State& state, int32_t y, const std::string& text, bool unlocked) {
    return DrawText(dl, state, kValueX, y, text, unlocked ? kBlack : kDimmed);
}

// constructor_menu0E_mpoptions().
Gfx* Construct(Gfx* dl, MenuStateMachine&) {
    State& state = GetState();
    if (!state.paper.Ready()) {
        return dl;
    }
    const FolderPaper& paper = state.paper;
    const MultiplayerSettings& settings = state.front->multiplayer;
    const MpOptions::RowUnlocks unlocks = MpOptions::UnlocksFor(settings.scenario);
    const auto highlighted = state.model.HighlightedRow();
    using Row = MpOptionsModel::Row;

    dl = state.paper.DrawBackground(dl, *state.front);
    dl = Text::SetupRdp(dl);

    dl = DrawText(dl, state, kTitleX, kTitleY, paper.String(TitleString::kMpOptions), kBlack);

    dl = DrawLabel(dl, state, kPlayersY, TitleString::kPlayers, highlighted == Row::Players, true);
    dl = DrawLabel(dl, state, kScenarioY, TitleString::kScenario, highlighted == Row::Scenario, true);
    dl = DrawLabel(dl, state, kStageY, TitleString::kLevel, highlighted == Row::Stage, unlocks.stageSelect);
    dl = DrawLabel(dl, state, kGameLengthY, TitleString::kGameLength, highlighted == Row::GameLength,
                   unlocks.gameLength);
    dl = DrawLabel(dl, state, kWeaponsY, TitleString::kWeapons, highlighted == Row::Weapons, unlocks.weaponSelect);
    dl = DrawLabel(dl, state, kCharacterY, TitleString::kCharacter, highlighted == Row::Character, unlocks.chars);
    dl = DrawLabel(dl, state, kHealthY, TitleString::kHealth, highlighted == Row::Health, unlocks.handicap);
    dl = DrawLabel(dl, state, kControlStyleY, TitleString::kControlStyle, highlighted == Row::ControlStyle,
                   unlocks.controlStyle);
    dl = DrawLabel(dl, state, kAimY, TitleString::kAim, highlighted == Row::Aim, unlocks.aimSight);

    dl = DrawValue(dl, state, kPlayersY, std::to_string(settings.playerCount), true);
    dl = DrawValue(dl, state, kScenarioY,
                   paper.String(TitleString::kFirstScenario + static_cast<size_t>(settings.scenario)), true);
    dl = DrawValue(dl, state, kStageY,
                   paper.String(TitleString::kMpStageFolderText[static_cast<size_t>(settings.stage)]),
                   unlocks.stageSelect);
    dl = DrawValue(dl, state, kGameLengthY,
                   paper.String(TitleString::kFirstGameLength + static_cast<size_t>(settings.gameLength)),
                   unlocks.gameLength);
    if (state.weaponStrings != nullptr) {
        const auto slot = static_cast<size_t>(MpOptions::WeaponSetTextSlot(settings.weaponSet));
        if (slot < state.weaponStrings->Strings.size()) {
            dl = DrawValue(dl, state, kWeaponsY, state.weaponStrings->Strings[slot], unlocks.weaponSelect);
        }
    }
    dl = DrawValue(dl, state, kAimY,
                   paper.String(TitleString::kFirstAimSetting + static_cast<size_t>(settings.aimSetting)),
                   unlocks.aimSight);

    dl = DrawPreviousTab(dl, paper.Gothic(), paper.String(TitleString::kPrevious), state.model.TabPrevHighlighted());
    dl = DrawStartTab(dl, paper.Gothic(), paper.String(TitleString::kStart), state.model.TabStartHighlighted());
    return DrawFrontCursor(dl, state.front->cursor, 0);
}

} // namespace

void RegisterMpOptions(MenuStateMachine& menu, const std::shared_ptr<Ship::ResourceManager>& resourceManager,
                       const std::shared_ptr<FrontState>& front) {
    State& state = GetState();
    state.front = front;
    state.resourceManager = resourceManager;
    if (front->walletBond == nullptr) {
        front->walletBond = std::make_shared<WalletBond>(resourceManager);
    }
    menu.RegisterMenu(MenuId::MultiplayerOptions, {
                                                      .onInit = Init,
                                                      .onInterface = Interface,
                                                      .onConstruct = Construct,
                                                  });
}

} // namespace Frigate::Game::Menu
