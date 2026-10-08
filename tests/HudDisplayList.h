#pragma once

#include <cstdint>
#include <vector>

#include "libultraship/libultra/gbi.h"

namespace Frigate::TestSupport {

// A filled or textured rectangle in a display list, in whole pixels, with the
// primitive colour (0xRRGGBBAA) that was set when it was drawn.
struct DrawnRect {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
    uint32_t colour = 0;
    bool textured = false;
};

// Every fill rectangle and texture rectangle in [begin, end).
inline std::vector<DrawnRect> ReadRects(const Gfx* begin, const Gfx* end) {
    std::vector<DrawnRect> rects;
    uint32_t colour = 0;
    for (const Gfx* command = begin; command < end; command++) {
        const uint32_t op = command->words.w0 >> 24;
        if (op == static_cast<uint8_t>(G_SETPRIMCOLOR)) {
            colour = command->words.w1;
        } else if (op == static_cast<uint8_t>(G_FILLRECT)) {
            DrawnRect rect;
            rect.right = static_cast<int32_t>((command->words.w0 >> 14) & 0x3FF);
            rect.bottom = static_cast<int32_t>((command->words.w0 >> 2) & 0x3FF);
            rect.left = static_cast<int32_t>((command->words.w1 >> 14) & 0x3FF);
            rect.top = static_cast<int32_t>((command->words.w1 >> 2) & 0x3FF);
            rect.colour = colour;
            rects.push_back(rect);
        } else if (op == static_cast<uint8_t>(G_TEXRECT)) {
            DrawnRect rect;
            rect.right = static_cast<int32_t>((command->words.w0 >> 12) & 0xFFF) / 4;
            rect.bottom = static_cast<int32_t>(command->words.w0 & 0xFFF) / 4;
            rect.left = static_cast<int32_t>((command->words.w1 >> 12) & 0xFFF) / 4;
            rect.top = static_cast<int32_t>(command->words.w1 & 0xFFF) / 4;
            rect.colour = colour;
            rect.textured = true;
            rects.push_back(rect);
        }
    }
    return rects;
}

} // namespace Frigate::TestSupport
