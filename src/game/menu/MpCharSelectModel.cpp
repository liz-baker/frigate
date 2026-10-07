#include "game/menu/MpCharSelectModel.h"

#include "game/menu/FileSelectModel.h"
#include "game/save/SaveProgress.h"

namespace Frigate::Game::Menu {

namespace {

namespace Button = Input::Button;

constexpr uint16_t kLeftButtons = Button::kPadLeft | Button::kCLeft;
constexpr uint16_t kRightButtons = Button::kPadRight | Button::kCRight;
constexpr uint16_t kSelectButtons = Button::kA | Button::kZ | Button::kStart;

// JOY_CLAMP_OFFSET and JOY_CLAMP_MAX.
constexpr int kStickOffset = 60;
constexpr int kStickMax = 120;

} // namespace

int MpCharSelectModel::StickXInRange(int8_t stickX) {
    int shifted = stickX + kStickOffset;
    if (shifted > kStickMax) {
        shifted = kStickMax;
    }
    if (shifted < 0) {
        shifted = 0;
    }
    return ((shifted * 3) / kStickMax) - 2;
}

void MpCharSelectModel::Init(FrontState& front) {
    front.tabNextSelected = false;
    front.tabPrevSelected = false;

    MultiplayerSettings& settings = front.multiplayer;
    const int players = SelectedPlayerCount(front.gameMode == GameMode::Multi, settings.playerCount);
    front.mpUnlocks.OnCharacterSelectEntered(*front.progress, players, settings.character, front.playerHasSelectedChar);
    numCharacters_ = front.mpUnlocks.NumCharsSelectable();

    for (size_t i = 0; i < kMpMaxPlayers; i++) {
        cursor_[i] = settings.character[i];
        previous_[i] = settings.character[i];
        scroll_[i] = 0;
        front.playerHasSelectedChar[i] = false;
        imageSize_[i] = 0;
    }
}

bool MpCharSelectModel::Update(const std::array<Input::Sample, kMpMaxPlayers>& inputs, FrontState& front) {
    sfx_.clear();
    MultiplayerSettings& settings = front.multiplayer;
    const int players = SelectedPlayerCount(front.gameMode == GameMode::Multi, settings.playerCount);
    int ready = 0;

    for (int player = 0; player < players && player < kMpMaxPlayers; player++) {
        const auto i = static_cast<size_t>(player);
        const Input::Sample& input = inputs[i];

        if (front.playerHasSelectedChar[i]) {
            if (imageSize_[i] < kFullSize && previous_[i] == cursor_[i]) {
                imageSize_[i]++;
            }
            if (input.Pressed(Button::kB)) {
                front.playerHasSelectedChar[i] = false;
                sfx_.push_back(FolderSfx::DoorMetalClose2);
            }
        }

        if (!front.playerHasSelectedChar[i] && imageSize_[i] > 0) {
            imageSize_[i]--;
        } else if (!front.playerHasSelectedChar[i]) {
            const int stick = StickXInRange(input.stickX);
            if (input.Pressed(kLeftButtons)) {
                if (cursor_[i] > 0) {
                    cursor_[i]--;
                }
            } else if (input.Pressed(kRightButtons)) {
                if (cursor_[i] < numCharacters_ - 1) {
                    cursor_[i]++;
                }
            } else if (input.Pressed(kSelectButtons)) {
                if (!PlayersHaveSelectedChar(cursor_[i], players, settings.character, front.playerHasSelectedChar)) {
                    settings.character[i] = cursor_[i];
                    imageSize_[i] = 1;
                    front.playerHasSelectedChar[i] = true;
                    sfx_.push_back(FolderSfx::DoorMetalClose2);
                }
            } else if (stick > 0) {
                if (scroll_[i] == 0 && cursor_[i] < numCharacters_ - 1) {
                    cursor_[i]++;
                }
            } else if (stick < -1 && scroll_[i] == 0) {
                if (cursor_[i] > 0) {
                    cursor_[i]--;
                }
            }
        }

        const int32_t position = (previous_[i] * kPortraitStride) + scroll_[i];
        if (cursor_[i] * kPortraitStride < position) {
            scroll_[i] -= kScrollStep;
            if (scroll_[i] < 0) {
                scroll_[i] += kPortraitStride;
                previous_[i]--;
            }
        } else if (position < cursor_[i] * kPortraitStride) {
            scroll_[i] += kScrollStep;
            if (scroll_[i] >= kPortraitStride) {
                scroll_[i] -= kPortraitStride;
                previous_[i]++;
            }
        }

        if (front.playerHasSelectedChar[i] && imageSize_[i] == kFullSize) {
            ready++;
        }
    }

    FileSelectModel::MoveCursorWithStick(inputs[0], front.cursor);
    return ready == players;
}

} // namespace Frigate::Game::Menu
