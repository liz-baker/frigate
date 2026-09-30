#include "level/Shading.h"

namespace Frigate::Level {

std::array<uint8_t, 3> TileRgb(const StanTile& tile) {
    const auto b = static_cast<uint8_t>((tile.mid >> 8) & 0xF);
    const auto c = static_cast<uint8_t>((tile.mid >> 4) & 0xF);
    const auto d = static_cast<uint8_t>(tile.mid & 0xF);
    return { static_cast<uint8_t>((b << 4) | b), static_cast<uint8_t>((c << 4) | c),
             static_cast<uint8_t>((d << 4) | d) };
}

ShadeColour ShadeFromTile(const StanTile& tile) {
    const std::array<uint8_t, 3> rgb = TileRgb(tile);
    ShadeColour col = { rgb[0], rgb[1], rgb[2], 0 };

    const int brightness = (col[0] * 79 + col[1] * 156 + col[2] * 21) >> 8;
    col[3] = static_cast<uint8_t>((255 - brightness) * 0.75f);

    int max = 0;
    int min = 0;
    int med = 0;

    if (col[1] > col[0]) {
        max = 1;
    } else {
        min = 1;
    }

    if (col[2] > col[static_cast<size_t>(max)]) {
        med = max;
        max = 2;
    } else if (col[2] > col[static_cast<size_t>(min)]) {
        med = 2;
    } else {
        med = min;
        min = 2;
    }

    if (col[static_cast<size_t>(max)] > 0) {
        const int scaled = col[static_cast<size_t>(med)] *
                           (col[static_cast<size_t>(max)] - col[static_cast<size_t>(min)]) /
                           col[static_cast<size_t>(max)];
        const int range = col[static_cast<size_t>(max)] - col[static_cast<size_t>(min)];
        col[static_cast<size_t>(min)] = 0;
        col[static_cast<size_t>(med)] = static_cast<uint8_t>(scaled);
        col[static_cast<size_t>(max)] = static_cast<uint8_t>(range);
    }

    col[0] >>= 1;
    col[1] >>= 1;
    col[2] >>= 1;
    return col;
}

ShadeColour ObjectShadeFromTile(const StanTile& tile) {
    ShadeColour col = ShadeFromTile(tile);
    col[0] >>= 1;
    col[1] >>= 1;
    col[2] >>= 1;
    return col;
}

void EaseShading(ShadeColour& dest, const ShadeColour& src) {
    for (size_t i = 0; i < 4; i++) {
        const int difference = src[i] - dest[i];
        dest[i] = static_cast<uint8_t>(dest[i] + ((difference + 7) >> 3));
    }
}

} // namespace Frigate::Level
