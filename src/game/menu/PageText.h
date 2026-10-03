#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "level/Lang.h"

namespace Frigate {
class Font;
}

namespace Frigate::Game::Menu {

// The text colours the folder's paper uses (0xRRGGBBAA): black, and the dark
// red of a failed objective.
namespace PageColour {
constexpr uint32_t kBlack = 0x000000FF;
constexpr uint32_t kFailedRed = 0x780000FF;
} // namespace PageColour

// One text draw (frontPrintText): the position it starts at.
struct TextLine {
    int32_t x = 0;
    int32_t y = 0;
    std::string text;
    uint32_t colour = PageColour::kBlack;
};

// getStringID(LTITLE, slot).
inline int32_t TitleId(int32_t slot) {
    return Level::StringId(Level::TextBank::Title, slot);
}

// What the pages' text layout needs of the font and the loaded string banks.
class PageText {
  public:
    struct Size {
        int32_t width = 0;
        int32_t height = 0;
    };
    virtual ~PageText() = default;
    // langGet.
    virtual std::string Get(int32_t id) const = 0;
    // textMeasure with the Zurich Bold font.
    virtual Size Measure(std::string_view text) const = 0;
    // textWrap with the Zurich Bold font; `indent` is text_wordwrap.
    virtual std::string Wrap(int32_t width, std::string_view text, int32_t indent) const = 0;
};

// The pages' text over the loaded string banks and the Zurich Bold font, both
// of which must outlive it.
class FontPageText final : public PageText {
  public:
    FontPageText(const Level::Lang& lang, const Font& font) : lang_(lang), font_(font) {
    }
    std::string Get(int32_t id) const override;
    Size Measure(std::string_view text) const override;
    std::string Wrap(int32_t width, std::string_view text, int32_t indent) const override;

  private:
    const Level::Lang& lang_;
    const Font& font_;
};

} // namespace Frigate::Game::Menu
