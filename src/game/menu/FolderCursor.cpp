#include "game/menu/FolderCursor.h"

#include <cmath>
#include <cstdint>

#include "level/ImageDraw.h"

namespace Frigate::Game::Menu {

namespace {

// The crosshair's entry in the global image table and its size.
constexpr int32_t kCrosshairOffset = 0xD2C;
constexpr int32_t kCrosshairWidth = 0x20;
constexpr int32_t kCrosshairHeight = 0x20;

} // namespace

Gfx* DrawCrosshairCursor(Gfx* dl, const MenuCursor& cursor) {
    const float position[2] = { std::floor(cursor.h + 0.5f), std::floor(cursor.v + 0.5f) };
    const float half[2] = { static_cast<float>(kCrosshairWidth) * 0.5f, static_cast<float>(kCrosshairHeight) * 0.5f };
    dl = Level::SelectImage(dl, kCrosshairOffset, 4);
    return Level::DisplayImageAtPosition(dl, position, half, kCrosshairWidth, kCrosshairHeight, false, false, true, 255,
                                         255, 255, 220);
}

} // namespace Frigate::Game::Menu
