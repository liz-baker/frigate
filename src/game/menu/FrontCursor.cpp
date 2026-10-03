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

Gfx* DrawTableImage(Gfx* dl, int32_t offset, int32_t width, int32_t height, float x, float y, uint8_t r, uint8_t g,
                    uint8_t b, uint8_t alpha) {
    const float position[2] = { x, y };
    const float half[2] = { static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f };
    dl = Level::SelectImage(dl, offset, 4);
    return Level::DisplayImageAtPosition(dl, position, half, width, height, false, false, true, r, g, b, alpha);
}

Gfx* DrawFrontCursor(Gfx* dl, const MenuCursor& cursor, int32_t optionIcon) {
    const TableImage* image = &kCrosshairImage;
    if (optionIcon == 1) {
        image = &kCopyImage;
    } else if (optionIcon == 2) {
        image = &kEraseImage;
    }
    return DrawTableImage(dl, image->offset, image->width, image->height, std::floor(cursor.h + 0.5f),
                          std::floor(cursor.v + 0.5f), 255, 255, 255, 220);
}

} // namespace Frigate::Game::Menu
