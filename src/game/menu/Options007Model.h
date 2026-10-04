#pragma once

#include <cstdint>
#include <vector>

#include "game/FrontState.h"
#include "game/Input.h"
#include "game/Sliders007.h"
#include "game/menu/FolderTabs.h"

namespace Frigate::Game::Menu {

// The slider bars (constructor_menu09_007options): 300 pixels wide from x 55.
constexpr int32_t kSliderLeft = 55;
constexpr float kSliderWidth = 300.0f;

// The bar's fill width for each slider. Health, damage and accuracy are 0 to 10
// and the bar shows their square root, which is how the cursor sets them.
int32_t SquaredBarWidth(float value);
inline int32_t ReactionBarWidth(float reaction) {
    return static_cast<int32_t>(reaction * kSliderWidth * 1);
}

// The percentage beside a bar. The accuracy's is the decomp's own: it scales
// by 10 where the others scale by 100.
inline int32_t SliderPercent(float value) {
    return static_cast<int32_t>(value * 100.0f);
}
inline int32_t AccuracyPercent(float value) {
    return static_cast<int32_t>(value * 10.0f);
}

// The 007 options screen's state and logic (init_menu09_007difficultyselect and
// interface_menu09_007options) apart from the drawing and the wallet model: the
// sliders, which the cursor sets while Z or A is held over a row, and the
// Start, Next and Previous tabs.
class Options007Model {
  public:
    enum class Next {
        Stay,
        Briefing,
        Difficulty,
        RunStage,
    };

    // init_menu09_007difficultyselect: nothing highlighted or chosen.
    void Init(bool& tabNextSelected, bool& tabPrevSelected);

    // One frame of interface_menu09_007options. Moves `cursor`, sets the row
    // `sliders` the cursor is over and raises sounds into Sfx().
    Next Update(const Input::Sample& input, MenuCursor& cursor, Sliders007& sliders, bool& tabNextSelected,
                bool& tabPrevSelected);

    bool TabStartHighlighted() const {
        return tabStartHighlight_;
    }
    bool TabNextHighlighted() const {
        return tabNextHighlight_;
    }
    bool TabPrevHighlighted() const {
        return tabPrevHighlight_;
    }
    bool ReactionHighlighted() const {
        return highlightReaction_;
    }
    bool HealthHighlighted() const {
        return highlightHealth_;
    }
    bool DamageHighlighted() const {
        return highlightDamage_;
    }
    bool AccuracyHighlighted() const {
        return highlightAccuracy_;
    }

    const std::vector<FolderSfx>& Sfx() const {
        return sfx_;
    }

  private:
    void UpdateHighlights(const MenuCursor& cursor);
    void ApplySlider(const MenuCursor& cursor, Sliders007& sliders) const;

    bool tabStartSelected_ = false;
    bool tabNextHighlight_ = false;
    bool tabPrevHighlight_ = false;
    bool tabStartHighlight_ = false;
    bool highlightReaction_ = false;
    bool highlightHealth_ = false;
    bool highlightDamage_ = false;
    bool highlightAccuracy_ = false;
    std::vector<FolderSfx> sfx_;
};

} // namespace Frigate::Game::Menu
