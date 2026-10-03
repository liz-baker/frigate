#include "game/menu/Options007Model.h"

#include <cmath>

#include "game/menu/FileSelectModel.h"

namespace Frigate::Game::Menu {

namespace {

namespace Button = Input::Button;

constexpr uint16_t kSelectButtons = Button::kZ | Button::kA;

// The rows' tops: the cursor is over a row from its y down, the first of them
// the Next tab's.
constexpr int32_t kReactionRow = 0x107;
constexpr int32_t kAccuracyRow = 0xE6;
constexpr int32_t kDamageRow = 0xC5;
constexpr int32_t kHealthRow = 0xA4;

} // namespace

int32_t SquaredBarWidth(float value) {
    return static_cast<int32_t>(std::sqrt(value / 10.0f) * kSliderWidth);
}

void Options007Model::Init(bool& tabNextSelected, bool& tabPrevSelected) {
    tabStartSelected_ = false;
    tabNextSelected = false;
    tabPrevSelected = false;
    tabPrevHighlight_ = false;
    tabNextHighlight_ = false;
    tabStartHighlight_ = false;
    highlightReaction_ = false;
    highlightHealth_ = false;
    highlightDamage_ = false;
    highlightAccuracy_ = false;
}

void Options007Model::UpdateHighlights(const MenuCursor& cursor) {
    tabPrevHighlight_ = false;
    tabNextHighlight_ = false;
    tabStartHighlight_ = false;
    highlightReaction_ = false;
    highlightHealth_ = false;
    highlightDamage_ = false;
    highlightAccuracy_ = false;

    if (CursorOnPreviousTab(cursor)) {
        tabPrevHighlight_ = true;
    } else if (CursorOnNextTab(cursor)) {
        tabNextHighlight_ = true;
    } else if (CursorOnStartTab(cursor)) {
        tabStartHighlight_ = true;
    } else {
        const auto y = static_cast<int32_t>(cursor.v);
        if (y >= kReactionRow) {
            highlightReaction_ = true;
        } else if (y >= kAccuracyRow) {
            highlightAccuracy_ = true;
        } else if (y >= kDamageRow) {
            highlightDamage_ = true;
        } else if (y >= kHealthRow) {
            highlightHealth_ = true;
        } else {
            tabNextHighlight_ = true;
        }
    }
}

void Options007Model::ApplySlider(const MenuCursor& cursor, Sliders007& sliders) const {
    float x = (cursor.h - 55.0f) / 300.0f;
    if (x > 1.0f) {
        x = 1.0f;
    }
    if (x < 0.0f) {
        x = 0.0f;
    }

    if (highlightReaction_) {
        sliders.reaction = x;
    } else if (highlightHealth_) {
        sliders.health = x * x * 10.0f;
    } else if (highlightDamage_) {
        sliders.damage = x * x * 10.0f;
    } else if (highlightAccuracy_) {
        sliders.accuracy = x * x * 10.0f;
    }
}

Options007Model::Next Options007Model::Update(const Input::Sample& input, MenuCursor& cursor, Sliders007& sliders,
                                              bool& tabNextSelected, bool& tabPrevSelected) {
    sfx_.clear();

    // While Z or A is held the highlight stays on the row it started on, so
    // the slider can be dragged off it.
    if (!input.Held(kSelectButtons)) {
        UpdateHighlights(cursor);
    }

    if (input.Pressed(Button::kStart)) {
        tabStartSelected_ = true;
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    } else if (input.Pressed(kSelectButtons)) {
        if (tabNextHighlight_) {
            tabNextSelected = true;
        } else if (tabPrevHighlight_) {
            tabPrevSelected = true;
        } else if (tabStartHighlight_) {
            tabStartSelected_ = true;
        }
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    } else if (input.Pressed(Button::kB)) {
        tabPrevSelected = true;
        sfx_.push_back(FolderSfx::DoorMetalClose2);
    }

    if (input.Held(kSelectButtons)) {
        ApplySlider(cursor, sliders);
    }

    FileSelectModel::MoveCursorWithStick(input, cursor);

    if (tabStartSelected_) {
        return Next::RunStage;
    }
    if (tabNextSelected) {
        return Next::Briefing;
    }
    if (tabPrevSelected) {
        return Next::Difficulty;
    }
    return Next::Stay;
}

} // namespace Frigate::Game::Menu
