#include "extract/TextureMip.h"

#include <algorithm>
#include <array>

namespace Frigate::Extract {

namespace {

// Squared five-bit channel range: the cost of an alpha mismatch in the RGBA closest-colour search.
constexpr int kAlphaWeight = 31 * 31;
constexpr size_t kPaletteTableSize = 0x100;

bool IsFourBit(TexFormat f) {
    return f == TexFormat::IA4 || f == TexFormat::I4 || f == TexFormat::RGBA16_CI4 || f == TexFormat::IA16_CI4;
}

bool IsEightBit(TexFormat f) {
    return f == TexFormat::IA8 || f == TexFormat::I8 || f == TexFormat::RGBA16_CI8 || f == TexFormat::IA16_CI8;
}

bool IsThirtyTwoBit(TexFormat f) {
    return f == TexFormat::RGBA32 || f == TexFormat::RGB24;
}

bool IsIaPalette(TexFormat f) {
    return f == TexFormat::IA16_CI8 || f == TexFormat::IA16_CI4;
}

size_t BytesPerRow(TexFormat format, size_t texels) {
    if (IsFourBit(format)) {
        return texels / 2;
    }
    if (IsEightBit(format)) {
        return texels;
    }
    if (IsThirtyTwoBit(format)) {
        return texels * 4;
    }
    return texels * 2;
}

size_t NativeRowBytes(TexFormat format, size_t width) {
    return IsFourBit(format) ? (width + 1) / 2 : BytesPerRow(format, width);
}

// Reads past the end give 0 instead of undefined behaviour; a well-formed level never does.
int At(std::span<const uint8_t> data, size_t index) {
    return index < data.size() ? data[index] : 0;
}

void Set(std::vector<uint8_t>& data, size_t index, int value) {
    if (index < data.size()) {
        data[index] = uint8_t(value);
    }
}

struct Rgba {
    int r, g, b, a;
};

Rgba RgbaAverage(const std::array<int, 4>& colours) {
    int r = 0, g = 0, b = 0, a = 0;
    for (int c : colours) {
        r += (c >> 11) & 0x1F;
        g += (c >> 6) & 0x1F;
        b += (c >> 1) & 0x1F;
        a += c & 1;
    }
    return { (r >> 2) & 0x1F, (g >> 2) & 0x1F, (b >> 2) & 0x1F, ((a + 2) >> 2) & 1 };
}

struct Ia {
    int intensity, alpha;
};

Ia IaAverage(const std::array<int, 4>& colours) {
    int i = 0, a = 0;
    for (int c : colours) {
        i += (c >> 8) & 0xFF;
        a += c & 0xFF;
    }
    return { (i >> 2) & 0xFF, ((a + 1) >> 2) & 0xFF };
}

} // namespace

uint32_t AlignedRowTexels(TexFormat format, uint32_t width) {
    if (IsFourBit(format)) {
        return (width + 15) & ~15u;
    }
    if (IsEightBit(format)) {
        return (width + 7) & ~7u;
    }
    return (width + 3) & ~3u;
}

std::vector<uint8_t> PadRows(TexFormat format, uint32_t width, uint32_t height, std::span<const uint8_t> data) {
    size_t srcRow = NativeRowBytes(format, width);
    size_t dstRow = BytesPerRow(format, AlignedRowTexels(format, width));
    std::vector<uint8_t> out(dstRow * height, 0);
    for (size_t y = 0; y < height; y++) {
        size_t begin = std::min(y * srcRow, data.size());
        size_t end = std::min((y + 1) * srcRow, data.size());
        std::copy(data.begin() + std::ptrdiff_t(begin), data.begin() + std::ptrdiff_t(end),
                  out.begin() + std::ptrdiff_t(y * dstRow));
    }
    return out;
}

size_t ClosestRgbaIndex(std::span<const uint16_t> palette, int r, int g, int b, int a) {
    int count = int(palette.size());
    int target = (r << 11) | (g << 6) | (b << 1) | a;
    for (int i = 0; i < count; i++) {
        if (palette[size_t(i)] == target) {
            return size_t(i);
        }
    }

    int low = 0, high = count - 1;
    int targetMagnitude = r * r + g * g + b * b + a * kAlphaWeight;
    while (high - low >= 2) {
        int mid = (high + low) >> 1;
        int colour = palette[size_t(mid)];
        int cr = (colour >> 11) & 0x1F, cg = (colour >> 6) & 0x1F, cb = (colour >> 1) & 0x1F, ca = colour & 1;
        int magnitude = cr * cr + cg * cg + cb * cb + ca * kAlphaWeight;
        if (magnitude < targetMagnitude) {
            low = mid;
        } else if (targetMagnitude < magnitude) {
            high = mid;
        } else {
            low = high = mid;
        }
    }

    int start = std::max(high - 4, 0);
    int end = std::min(high + 4, count - 1);
    int bestIndex = 0, bestDistance = 999999;
    for (int i = start; i <= end; i++) {
        int colour = palette[size_t(i)];
        int dr = ((colour >> 11) & 0x1F) - r;
        int dg = ((colour >> 6) & 0x1F) - g;
        int db = ((colour >> 1) & 0x1F) - b;
        int distance = (a == (colour & 1) ? 0 : kAlphaWeight) + dr * dr + dg * dg + db * db;
        if (distance < bestDistance) {
            bestIndex = i;
            bestDistance = distance;
        }
    }
    return size_t(bestIndex);
}

size_t ClosestIaIndex(std::span<const uint16_t> palette, int intensity, int alpha) {
    int count = int(palette.size());
    int target = ((intensity << 8) | alpha) & 0xFFFF;
    for (int i = 0; i < count; i++) {
        if (palette[size_t(i)] == target) {
            return size_t(i);
        }
    }

    int low = 0, high = count - 1;
    int targetMagnitude = intensity * intensity + alpha * alpha;
    while (high - low >= 2) {
        int mid = (high + low) >> 1;
        int colour = palette[size_t(mid)];
        int ci = (colour >> 8) & 0xFF, ca = colour & 0xFF;
        int magnitude = ci * ci + ca * ca;
        if (magnitude < targetMagnitude) {
            low = mid;
        } else if (targetMagnitude < magnitude) {
            high = mid;
        } else {
            low = high = mid;
        }
    }

    int start = std::max(high - 4, 0);
    int end = std::min(high + 4, count - 1);
    int bestIndex = 0, bestDistance = 999999;
    for (int i = start; i <= end; i++) {
        int colour = palette[size_t(i)];
        int di = ((colour >> 8) & 0xFF) - intensity;
        int da = (colour & 0xFF) - alpha;
        int distance = di * di + da * da;
        if (distance < bestDistance) {
            bestIndex = i;
            bestDistance = distance;
        }
    }
    return size_t(bestIndex);
}

// Halves a paletted level (rows TMEM-padded) by averaging each 2x2 block of palette colours and mapping the
// average back to the closest entry of the same palette.
std::vector<uint8_t> ShrinkPaletted(TexFormat format, uint32_t width, uint32_t height, std::span<const uint8_t> src,
                                    std::span<const uint16_t> palette) {
    const size_t srcAligned = AlignedRowTexels(format, width);
    const size_t dstAligned = AlignedRowTexels(format, (width + 1) >> 1);
    const size_t dstHeight = (height + 1) >> 1;
    const bool ia = IsIaPalette(format);

    auto closest = [&](const std::array<int, 4>& colours) -> int {
        if (ia) {
            Ia avg = IaAverage(colours);
            return int(ClosestIaIndex(palette, avg.intensity, avg.alpha));
        }
        Rgba avg = RgbaAverage(colours);
        return int(ClosestRgbaIndex(palette, avg.r, avg.g, avg.b, avg.a));
    };

    // The game reads colours from a 256-entry table holding only the image's own entries; the IA quirk below can
    // read entry 4 of a smaller palette, whose contents the game leaves to whatever an earlier load put there.
    // That can't be known when packing, so those entries read as 0.
    std::array<int, kPaletteTableSize> table = {};
    for (size_t i = 0; i < palette.size() && i < kPaletteTableSize; i++) {
        table[i] = palette[i];
    }

    std::vector<uint8_t> out(BytesPerRow(format, dstAligned) * dstHeight, 0);

    if (IsEightBit(format)) {
        size_t outRow = 0;
        for (size_t y = 0; y < height; y += 2, outRow++) {
            size_t row = y * srcAligned;
            size_t below = row + (y + 1 < height ? srcAligned : 0);
            for (size_t x = 0; x < srcAligned; x += 2) {
                size_t right = x + 1 < width ? x + 1 : x;
                std::array<int, 4> colours = { table[size_t(At(src, row + x))], table[size_t(At(src, row + right))],
                                               table[size_t(At(src, below + x))],
                                               table[size_t(At(src, below + right))] };
                Set(out, outRow * dstAligned + (x >> 1), closest(colours));
            }
        }
        return out;
    }

    const size_t rowBytes = srcAligned >> 1;
    const size_t dstRowBytes = dstAligned >> 1;
    size_t outRow = 0;
    for (size_t y = 0; y < height; y += 2, outRow++) {
        size_t row = y * rowBytes;
        size_t below = row + (y + 1 < height ? rowBytes : 0);
        for (size_t x = 0; x < srcAligned; x += 4) {
            int byte = 0;
            for (int half = 0; half < 2; half++) {
                size_t pair = x + size_t(half) * 2;
                size_t last = pair + 1;
                int top = At(src, row + (pair >> 1));
                int bottom = At(src, below + (pair >> 1));
                int rightTop, rightBottom;
                if (ia) {
                    // The IA path's precedence slip: the shift by the in-range test feeds the ?: as its
                    // condition, so the right-hand texels read palette entry 0 or 4 instead of their own.
                    int inRange = last < width ? 1 : 0;
                    rightTop = table[(top >> inRange) != 0 ? 0 : 4];
                    rightBottom = table[(bottom >> inRange) != 0 ? 0 : 4];
                } else {
                    int shift = last < width ? 0 : 4;
                    rightTop = table[size_t((top >> shift) & 0xF)];
                    rightBottom = table[size_t((bottom >> shift) & 0xF)];
                }
                std::array<int, 4> colours = { table[size_t((top >> 4) & 0xF)], rightTop,
                                               table[size_t((bottom >> 4) & 0xF)], rightBottom };
                int index = closest(colours);
                byte = half == 0 ? ((index << 4) & 0xFF) : (byte | (index & 0xFF));
            }
            Set(out, outRow * dstRowBytes + (x >> 2), byte & 0xFF);
        }
    }
    return out;
}

// Halves a non-paletted level (rows TMEM-padded) by averaging each 2x2 block, channel by channel, with the
// game's per-format rounding.
std::vector<uint8_t> ShrinkNonPaletted(TexFormat format, uint32_t width, uint32_t height,
                                       std::span<const uint8_t> src) {
    const size_t srcAligned = AlignedRowTexels(format, width);
    const size_t dstAligned = AlignedRowTexels(format, (width + 1) >> 1);
    const size_t dstHeight = (height + 1) >> 1;
    std::vector<uint8_t> out(BytesPerRow(format, dstAligned) * dstHeight, 0);

    auto quad = [&](size_t y, size_t x) -> std::array<size_t, 4> {
        size_t belowOffset = y + 1 < height ? srcAligned : 0;
        size_t right = x + 1 < width ? x + 1 : x;
        return { y * srcAligned + x, y * srcAligned + right, y * srcAligned + belowOffset + x,
                 y * srcAligned + belowOffset + right };
    };
    auto word32 = [&](size_t index) -> uint32_t {
        return (uint32_t(At(src, index * 4)) << 24) | (uint32_t(At(src, index * 4 + 1)) << 16) |
               (uint32_t(At(src, index * 4 + 2)) << 8) | uint32_t(At(src, index * 4 + 3));
    };
    auto word16 = [&](size_t index) -> int { return (At(src, index * 2) << 8) | At(src, index * 2 + 1); };

    if (IsThirtyTwoBit(format)) {
        size_t outRow = 0;
        for (size_t y = 0; y < height; y += 2, outRow++) {
            for (size_t x = 0; x < srcAligned; x += 2) {
                uint32_t r = 0, g = 0, b = 0, a = 0;
                for (size_t i : quad(y, x)) {
                    uint32_t w = word32(i);
                    r += (w >> 24) & 0xFF;
                    g += (w >> 16) & 0xFF;
                    b += (w >> 8) & 0xFF;
                    a += w & 0xFF;
                }
                uint32_t value = (((r >> 2) & 0xFF) << 24) | (((g >> 2) & 0xFF) << 16) | (((b >> 2) & 0xFF) << 8) |
                                 (((a + 1) >> 2) & 0xFF);
                size_t at = (outRow * dstAligned + (x >> 1)) * 4;
                Set(out, at, int(value >> 24));
                Set(out, at + 1, int(value >> 16));
                Set(out, at + 2, int(value >> 8));
                Set(out, at + 3, int(value));
            }
        }
        return out;
    }

    if (format == TexFormat::RGBA16 || format == TexFormat::RGB15 || format == TexFormat::IA16) {
        size_t outRow = 0;
        for (size_t y = 0; y < height; y += 2, outRow++) {
            for (size_t x = 0; x < srcAligned; x += 2) {
                std::array<int, 4> texels;
                std::array<size_t, 4> q = quad(y, x);
                for (size_t i = 0; i < 4; i++) {
                    texels[i] = word16(q[i]);
                }
                int value;
                if (format == TexFormat::IA16) {
                    int intensity = 0, alpha = 0;
                    for (int t : texels) {
                        intensity += (t >> 8) & 0xFF;
                        alpha += t & 0xFF;
                    }
                    value = (((intensity >> 2) & 0xFF) << 8) | (((alpha + 1) >> 2) & 0xFF);
                } else {
                    Rgba avg = RgbaAverage(texels);
                    value = (avg.r << 11) | (avg.g << 6) | (avg.b << 1) | avg.a;
                }
                size_t at = (outRow * dstAligned + (x >> 1)) * 2;
                Set(out, at, value >> 8);
                Set(out, at + 1, value);
            }
        }
        return out;
    }

    if (format == TexFormat::IA8 || format == TexFormat::I8) {
        size_t outRow = 0;
        for (size_t y = 0; y < height; y += 2, outRow++) {
            for (size_t x = 0; x < srcAligned; x += 2) {
                int sum = 0, high = 0, low = 0;
                for (size_t i : quad(y, x)) {
                    int t = At(src, i);
                    sum += t;
                    high += (t >> 4) & 0xF;
                    low += t & 0xF;
                }
                int value = format == TexFormat::IA8 ? ((high << 2) & 0xF0) | (((low + 1) >> 2) & 0xF) : (sum + 1) >> 2;
                Set(out, outRow * dstAligned + (x >> 1), value & 0xFF);
            }
        }
        return out;
    }

    // 4-bit: each output byte comes from two source bytes in the row and the two below them. The game averages the
    // two nibbles of the left byte with those of the one below for the high output nibble, and likewise for the
    // right pair, rather than forming true 2x2 blocks.
    const size_t rowBytes = srcAligned >> 1;
    const size_t dstRowBytes = dstAligned >> 1;
    size_t outRow = 0;
    for (size_t y = 0; y < height; y += 2, outRow++) {
        size_t row = y * rowBytes;
        size_t below = row + (y + 1 < height ? rowBytes : 0);
        for (size_t x = 0; x < srcAligned; x += 4) {
            int tl = At(src, row + (x >> 1));
            int tr = At(src, below + (x >> 1));
            int bl = At(src, row + (x >> 1) + 1);
            int br = At(src, below + (x >> 1) + 1);
            int value;
            if (format == TexFormat::IA4) {
                int high = ((((tl >> 5) & 7) + ((tl >> 1) & 7) + ((tr >> 5) & 7) + ((tr >> 1) & 7)) << 3) & 0xE0;
                int low = ((((bl >> 5) & 7) + ((bl >> 1) & 7) + ((br >> 5) & 7) + ((br >> 1) & 7)) >> 1) & 0xE;
                int highAlpha = ((((tl >> 4) & 1) + (tl & 1) + ((tr >> 4) & 1) + (tr & 1) + 1) << 2) & 0x10;
                int lowAlpha = ((((bl >> 4) & 1) + (bl & 1) + ((br >> 4) & 1) + (br & 1) + 1) >> 2) & 1;
                value = high | low | highAlpha | lowAlpha;
            } else {
                int high = ((((tl >> 4) & 0xF) + (tl & 0xF) + ((tr >> 4) & 0xF) + (tr & 0xF)) << 2) & 0xF0;
                int low = ((((bl >> 4) & 0xF) + (bl & 0xF) + ((br >> 4) & 0xF) + (br & 0xF)) >> 2) & 0xF;
                value = high | low;
            }
            Set(out, outRow * dstRowBytes + (x >> 2), value & 0xFF);
        }
    }
    return out;
}

std::optional<std::vector<MipLevel>> BuildMipChain(const DecodedTexture& texture) {
    if (texture.header.lodCount < 2) {
        return std::nullopt;
    }
    const TexFormat format = texture.header.format;
    // TODO(https://github.com/liz-baker/frigate/issues/458): a texture with explicit LODs carries its own lower
    // levels in the bitstream; until those are decoded, its chain is generated like any other.
    if (IsThirtyTwoBit(format)) {
        // TODO(https://github.com/liz-baker/frigate/issues/459): a 32-bit level splits across TMEM's two banks,
        // which libultraship's mip chain path doesn't read.
        return std::nullopt;
    }

    std::vector<MipLevel> levels;
    levels.push_back({ texture.header.width, texture.header.height,
                       PadRows(format, texture.header.width, texture.header.height, texture.pixels) });
    size_t total = levels[0].data.size();
    const bool paletted = IsPaletted(format);

    for (uint32_t level = 1; level < texture.header.lodCount; level++) {
        const MipLevel& src = levels.back();
        std::vector<uint8_t> data = paletted
                                        ? ShrinkPaletted(format, src.width, src.height, src.data, texture.paletteNative)
                                        : ShrinkNonPaletted(format, src.width, src.height, src.data);
        if (paletted && total + data.size() > kPalettedChainLimit) {
            break;
        }
        total += data.size();
        MipLevel next = { (src.width + 1) >> 1, (src.height + 1) >> 1, std::move(data) };
        levels.push_back(std::move(next));
    }

    if (levels.size() < 2) {
        return std::nullopt;
    }
    return levels;
}

} // namespace Frigate::Extract
