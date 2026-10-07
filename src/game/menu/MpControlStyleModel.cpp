#include "game/menu/MpControlStyleModel.h"

#include "game/menu/FileSelectModel.h"
#include "game/menu/StickRange.h"

namespace Frigate::Game::Menu {

namespace {

namespace Button = Input::Button;
using Level::ControlConfig;

constexpr uint16_t kLeftButtons = Button::kPadLeft | Button::kCLeft;
constexpr uint16_t kRightButtons = Button::kPadRight | Button::kCRight;
constexpr uint16_t kSelectButtons = Button::kA | Button::kZ | Button::kStart;

constexpr size_t kPlayer1 = 0;
constexpr size_t kPlayer2 = 1;

ControlConfig Next(ControlConfig style) {
    return static_cast<ControlConfig>(static_cast<int>(style) + 1);
}

ControlConfig Previous(ControlConfig style) {
    return static_cast<ControlConfig>(static_cast<int>(style) - 1);
}

} // namespace

void MpControlStyleModel::Init(FrontState& front) {
    front.tabNextSelected = false;
    front.tabPrevSelected = false;
    for (size_t i = 0; i < kMpMaxPlayers; i++) {
        front.playerHasSelectedChar[i] = false;
        stickReset_[i] = false;
    }
}

void MpControlStyleModel::StepPrevious(std::array<ControlConfig, kMpMaxPlayers>& styles, int player, int players) {
    const auto i = static_cast<size_t>(player);
    if (styles[i] <= ControlConfig::Honey) {
        return;
    }
    // While the first player has a two-controller style the second player
    // cannot step below 2.1 (the first player's step off 2.1 resets them).
    if (players != 2 || i != kPlayer2 || styles[kPlayer1] < ControlConfig::Plenty ||
        styles[i] >= ControlConfig::Galore) {
        styles[i] = Previous(styles[i]);
    }
    if (players == 2 && i == kPlayer1 && styles[kPlayer1] < ControlConfig::Plenty &&
        styles[kPlayer2] >= ControlConfig::Plenty) {
        styles[kPlayer2] = ControlConfig::Honey;
    }
}

void MpControlStyleModel::StepNext(std::array<ControlConfig, kMpMaxPlayers>& styles, int player, int players) {
    const auto i = static_cast<size_t>(player);
    if ((players == 2 && i == kPlayer1) || styles[i] >= ControlConfig::Plenty) {
        if (styles[i] < ControlConfig::Goodhead) {
            styles[i] = Next(styles[i]);
            if (i == kPlayer1 && styles[kPlayer1] == ControlConfig::Plenty &&
                styles[kPlayer2] < ControlConfig::Plenty) {
                styles[kPlayer2] = ControlConfig::Plenty;
            }
        }
    } else if (styles[i] < ControlConfig::Goodnight) {
        styles[i] = Next(styles[i]);
    }
}

bool MpControlStyleModel::Update(const std::array<Input::Sample, kMpMaxPlayers>& inputs, FrontState& front) {
    sfx_.clear();
    MultiplayerSettings& settings = front.multiplayer;
    const int players = SelectedPlayerCount(front.gameMode == GameMode::Multi, settings.playerCount);
    int ready = 0;

    for (int player = 0; player < players && player < kMpMaxPlayers; player++) {
        const auto i = static_cast<size_t>(player);
        const Input::Sample& input = inputs[i];

        if (front.playerHasSelectedChar[i] && input.Pressed(Button::kB)) {
            front.playerHasSelectedChar[i] = false;
            sfx_.push_back(FolderSfx::DoorMetalClose2);
        }

        if (!front.playerHasSelectedChar[i]) {
            const int stick = StickInRange(input.stickX);
            if (input.Pressed(kLeftButtons) || (stick < -1 && stickReset_[i])) {
                StepPrevious(settings.controlStyle, player, players);
            } else if (input.Pressed(kRightButtons) || (stick > 0 && stickReset_[i])) {
                StepNext(settings.controlStyle, player, players);
            } else if (input.Pressed(kSelectButtons)) {
                front.playerHasSelectedChar[i] = true;
                sfx_.push_back(FolderSfx::DoorMetalClose2);
            }

            stickReset_[i] = stick == -1 || stick == 0;
        }

        if (front.playerHasSelectedChar[i]) {
            ready++;
        }
    }

    FileSelectModel::MoveCursorWithStick(inputs[0], front.cursor);
    return ready == players;
}

} // namespace Frigate::Game::Menu
