#include "game/menu/MpOptionsModel.h"

#include "game/menu/FileSelectModel.h"
#include "game/menu/StageStart.h"
#include "game/save/SaveProgress.h"

namespace Frigate::Game::Menu {

namespace {

namespace Button = Input::Button;

constexpr uint16_t kSelectButtons = Button::kZ | Button::kA;

} // namespace

void MpOptionsModel::Init(FrontState& front, int controllerCount) {
    tabStartSelected_ = false;
    front.tabNextSelected = false;
    front.tabPrevSelected = false;
    tabPrevHighlight_ = false;
    tabStartHighlight_ = false;
    row_.reset();

    MultiplayerSettings& settings = front.multiplayer;
    if (settings.playerCount == 0) {
        MpOptions::InitForPlayerCount(settings, controllerCount);
    }

    const int players = SelectedPlayerCount(front.gameMode == GameMode::Multi, settings.playerCount);
    settings.stage = static_cast<MpStage>(
        MultiplayerUnlocks::StageWhenOpeningOptions(*front.progress, static_cast<int>(settings.stage), players));
}

void MpOptionsModel::UpdateHighlights(const FrontState& front) {
    const MpOptions::RowUnlocks unlocks = MpOptions::UnlocksFor(front.multiplayer.scenario);

    tabPrevHighlight_ = false;
    tabStartHighlight_ = false;
    row_.reset();

    if (CursorOnPreviousTab(front.cursor)) {
        tabPrevHighlight_ = true;
    } else if (CursorOnStartTab(front.cursor)) {
        tabStartHighlight_ = true;
    } else {
        const auto v = static_cast<int32_t>(front.cursor.v);
        if (v >= kAimRow && unlocks.aimSight) {
            row_ = Row::Aim;
        } else if (v >= kControlStyleRow && unlocks.controlStyle) {
            row_ = Row::ControlStyle;
        } else if (v >= kHealthRow && unlocks.handicap) {
            row_ = Row::Health;
        } else if (v >= kCharacterRow && unlocks.chars) {
            row_ = Row::Character;
        } else if (v >= kWeaponsRow && unlocks.weaponSelect) {
            row_ = Row::Weapons;
        } else if (v >= kGameLengthRow && unlocks.gameLength) {
            row_ = Row::GameLength;
        } else if (v >= kStageRow && unlocks.stageSelect) {
            row_ = Row::Stage;
        } else if (v >= kScenarioRow) {
            row_ = Row::Scenario;
        } else {
            row_ = Row::Players;
        }
    }
}

const std::vector<MpOptionsModel::Next>& MpOptionsModel::Update(const Input::Sample& input, FrontState& front,
                                                                int controllerCount,
                                                                const std::function<uint32_t()>& nextRandom) {
    sfx_.clear();
    next_.clear();
    MultiplayerSettings& settings = front.multiplayer;

    if (controllerCount < 2) {
        next_.push_back(Next::ModeSelect);
        SetCursorForMode(front.cursor, static_cast<int>(front.gameMode));
    }

    if (controllerCount < settings.playerCount) {
        MpOptions::InitForPlayerCount(settings, controllerCount);
    }

    if (!input.Held(kSelectButtons)) {
        UpdateHighlights(front);
    }

    std::optional<Row> chosen;
    if (input.Pressed(Button::kStart)) {
        tabStartSelected_ = true;
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    } else if (input.Pressed(kSelectButtons)) {
        if (tabPrevHighlight_) {
            front.tabPrevSelected = true;
        }
        if (tabStartHighlight_) {
            tabStartSelected_ = true;
        } else {
            chosen = row_;
        }
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    } else if (input.Pressed(Button::kB)) {
        front.tabPrevSelected = true;
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    }

    FileSelectModel::MoveCursorWithStick(input, front.cursor);

    if (front.tabPrevSelected) {
        next_.push_back(Next::ModeSelect);
        SetCursorForMode(front.cursor, static_cast<int>(front.gameMode));
        return next_;
    }

    if (tabStartSelected_) {
        // A Random stage is drawn into selected_stage only: MP_stage_selected
        // stays Random, so the next visit to the stage select still shows it.
        const MpStage chosenStage = settings.stage;
        MultiplayerSettings game = settings;
        if (!game.StageLevel()) {
            const int players = SelectedPlayerCount(front.gameMode == GameMode::Multi, settings.playerCount);
            game.stage =
                static_cast<MpStage>(MultiplayerUnlocks::ResolveRandomStage(*front.progress, players, nextRandom));
        }
        if (!SelectMultiplayerStage(front, game)) {
            tabStartSelected_ = false;
            return next_;
        }
        front.multiplayer.stage = chosenStage;
        front.briefingPage = -1;
        next_.push_back(Next::RunStage);
        return next_;
    }

    if (!chosen) {
        return next_;
    }
    switch (*chosen) {
        case Row::Players:
            MpOptions::InitForPlayerCount(settings,
                                          controllerCount < settings.playerCount + 1 ? 2 : settings.playerCount + 1);
            break;
        case Row::Scenario:
            next_.push_back(Next::ScenarioSelect);
            break;
        case Row::Stage:
            next_.push_back(Next::StageSelect);
            break;
        case Row::GameLength:
            MpOptions::SelectGameLength(settings);
            break;
        case Row::Character:
            next_.push_back(Next::CharacterSelect);
            break;
        case Row::Weapons:
            MpOptions::IncrementWeaponSet(settings);
            break;
        case Row::Health:
            next_.push_back(Next::Handicap);
            break;
        case Row::ControlStyle:
            next_.push_back(Next::ControlStyle);
            break;
        case Row::Aim:
            MpOptions::AdvanceAimSetting(settings);
            front.multiplayerPlayerAim = MpOptions::CopyAimSettingsToPlayerData(settings);
            break;
    }
    return next_;
}

} // namespace Frigate::Game::Menu
