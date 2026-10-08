#include "game/menu/MpTeamsModel.h"

#include "game/menu/FileSelectModel.h"
#include "game/menu/StickRange.h"

namespace Frigate::Game::Menu {

namespace {

namespace Button = Input::Button;

constexpr uint16_t kRightButtons = Button::kPadRight | Button::kCRight;
constexpr uint16_t kLeftButtons = Button::kPadLeft | Button::kCLeft;
constexpr uint16_t kUpButtons = Button::kPadUp | Button::kCUp;
constexpr uint16_t kDownButtons = Button::kPadDown | Button::kCDown;
constexpr uint16_t kSelectButtons = Button::kA | Button::kZ | Button::kStart;

} // namespace

void MpTeamsModel::Init(FrontState& front) {
    front.tabNextSelected = false;
    front.tabPrevSelected = false;
    front.multiplayer.teamSize = front.multiplayer.scenario == MpScenario::TwoVsOne ? 2 : 3;
    flash_ = 0;
}

bool MpTeamsModel::Update(const Input::Sample& input, FrontState& front) {
    sfx_.clear();
    MultiplayerSettings& settings = front.multiplayer;
    const MpScenario scenario = settings.scenario;
    int& teamSize = settings.teamSize;

    flash_ = (flash_ + 1) % (kFlashFrames * 2);

    const int stickX = StickInRange(input.stickX);
    const int stickY = StickInRange(input.stickY);
    if (input.Pressed(kRightButtons) || stickX > 0) {
        if (scenario == MpScenario::TwoVsOne) {
            if ((teamSize & 1) == 0) {
                teamSize = 1;
                sfx_.push_back(FolderSfx::DoorMetalClose2);
            }
        } else if ((teamSize & 1) == 0) {
            teamSize += 1;
            sfx_.push_back(FolderSfx::DoorMetalClose2);
        }
    } else if (input.Pressed(kUpButtons) || stickY > 0) {
        if (scenario == MpScenario::TwoVsTwo) {
            if ((teamSize & 2) != 0) {
                teamSize = 1;
                sfx_.push_back(FolderSfx::DoorMetalClose2);
            }
        } else if ((teamSize & 2) != 0) {
            teamSize -= 2;
            sfx_.push_back(FolderSfx::DoorMetalClose2);
        }
    } else if (input.Pressed(kLeftButtons) || stickX < -1) {
        if (scenario == MpScenario::TwoVsTwo) {
            if ((teamSize & 1) != 0) {
                teamSize = 2;
                sfx_.push_back(FolderSfx::DoorMetalClose2);
            }
        } else if ((teamSize & 1) != 0) {
            teamSize -= 1;
            sfx_.push_back(FolderSfx::DoorMetalClose2);
        }
    } else if (input.Pressed(kDownButtons) || stickY < -1) {
        if (scenario == MpScenario::TwoVsOne) {
            if ((teamSize & 2) == 0) {
                teamSize = 2;
                sfx_.push_back(FolderSfx::DoorMetalClose2);
            }
        } else if ((teamSize & 2) == 0) {
            teamSize += 2;
            sfx_.push_back(FolderSfx::DoorMetalClose2);
        }
    }

    FileSelectModel::MoveCursorWithStick(input, front.cursor);

    if (input.Pressed(kSelectButtons)) {
        sfx_.push_back(FolderSfx::DoorMetalClose2);
        return true;
    }
    return false;
}

bool MpTeamsModel::TextShown(const FrontState& front, int player) const {
    return player != front.multiplayer.teamSize || flash_ < kFlashFrames;
}

} // namespace Frigate::Game::Menu
