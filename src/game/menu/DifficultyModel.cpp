#include "game/menu/DifficultyModel.h"

#include "game/menu/FileSelectModel.h"
#include "game/menu/MissionSelectModel.h"

namespace Frigate::Game::Menu {

namespace {

namespace Button = Input::Button;

constexpr uint16_t kSelectButtons = Button::kStart | Button::kZ | Button::kA;

// The cursor rows' tops: a row is highlighted from its y down.
constexpr float k007Row = 275.0f;
constexpr float k00Row = 243.0f;
constexpr float kSecretRow = 211.0f;

} // namespace

void DifficultyModel::Init(DifficultySelection& selection, bool& tabNextSelected, bool& tabPrevSelected) {
    selection.Set(Save::kDifficultyMulti);
    tabNextSelected = false;
    tabPrevSelected = false;
}

DifficultyModel::Next DifficultyModel::Update(const Input::Sample& input, int32_t unlocked, int32_t level,
                                              MenuCursor& cursor, DifficultySelection& selection, bool& tabNextSelected,
                                              bool& tabPrevSelected) {
    sfx_.clear();
    tabPrevHighlight_ = false;
    highlighted_ = Save::kDifficultyMulti;

    if (CursorOnPreviousTab(cursor)) {
        tabPrevHighlight_ = true;
    } else if (unlocked >= Save::kDifficulty007 && cursor.v >= k007Row) {
        highlighted_ = Save::kDifficulty007;
    } else if (unlocked >= Save::kDifficulty00 && cursor.v >= k00Row) {
        highlighted_ = Save::kDifficulty00;
    } else if (unlocked > Save::kDifficultyAgent && cursor.v >= kSecretRow) {
        highlighted_ = Save::kDifficultySecret;
    } else {
        highlighted_ = Save::kDifficultyAgent;
    }

    if (input.Pressed(kSelectButtons)) {
        if (tabPrevHighlight_) {
            tabPrevSelected = true;
            sfx_.push_back(FolderSfx::DoorMetalClose2);
        } else if (highlighted_ >= Save::kDifficultyAgent) {
            selection.Set(highlighted_);
            tabNextSelected = true;
            sfx_.push_back(FolderSfx::PaperTurn);
        }
    } else if (input.Pressed(Button::kB)) {
        tabPrevSelected = true;
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    }

    FileSelectModel::MoveCursorWithStick(input, cursor);

    if (tabNextSelected) {
        SetCursorToNextTab(cursor);
        return selection.Get() == Save::kDifficulty007 ? Next::Options007 : Next::Briefing;
    }
    if (tabPrevSelected) {
        if (level >= 0) {
            MissionSelectModel::PlaceCursorOnStage(level, cursor);
        }
        return Next::MissionSelect;
    }
    return Next::Stay;
}

} // namespace Frigate::Game::Menu
