#include "game/menu/FrontCursor.h"

#include <cmath>

#include "level/ImageDraw.h"

namespace Frigate::Game::Menu {

namespace {

// The global image table entries of the crosshair, IMG_COPY and IMG_DEL, with
// the sizes the table gives them.
struct TableImage {
    int32_t offset;
    int32_t width;
    int32_t height;
};
constexpr TableImage kCrosshairImage = { 0xD2C, 0x20, 0x20 };
constexpr TableImage kCopyImage = { 0xFD8, 0x20, 0x1C };
constexpr TableImage kEraseImage = { 0xFE4, 0x20, 0x1C };

} // namespace

Gfx* DrawFrontCursor(Gfx* dl, const MenuCursor& cursor, int32_t optionIcon) {
    const TableImage* image = &kCrosshairImage;
    if (optionIcon == 1) {
        image = &kCopyImage;
    } else if (optionIcon == 2) {
        image = &kEraseImage;
    }
    const float position[2] = { std::floor(cursor.h + 0.5f), std::floor(cursor.v + 0.5f) };
    const float half[2] = { static_cast<float>(image->width) * 0.5f, static_cast<float>(image->height) * 0.5f };
    dl = Level::SelectImage(dl, image->offset, 4);
    return Level::DisplayImageAtPosition(dl, position, half, image->width, image->height, false, false, true, 255, 255,
                                         255, 220);
}

} // namespace Frigate::Game::Menu
