#include "game/menu/PageText.h"

#include "game/TextRender.h"

namespace Frigate::Game::Menu {

std::string FontPageText::Get(int32_t id) const {
    return std::string(lang_.Get(id));
}

PageText::Size FontPageText::Measure(std::string_view text) const {
    const Text::Extent extent = Text::Measure(font_, text);
    return { extent.width, extent.height };
}

std::string FontPageText::Wrap(int32_t width, std::string_view text, int32_t indent) const {
    return Text::Wrap(width, text, font_, indent);
}

} // namespace Frigate::Game::Menu
