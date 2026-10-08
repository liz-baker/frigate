#include "game/menu/BloodAnimation.h"

#include <utility>

#include "gfx/TextureCache.h"

namespace Frigate::Game::Menu {

namespace {

// The decoded picture is kSourceWidth columns by kSourceHeight rows.
constexpr int kSourceWidth = 80;
constexpr int kSourceHeight = 96;
constexpr size_t kPixels = kSourceWidth * kSourceHeight;

using Pixels = std::array<uint8_t, kPixels>;

// Bounds-checked byte cursor: a stream that ends early reads as 0xFF, which
// ends every loop below.
struct Cursor {
    const std::vector<uint8_t>& data;
    size_t position;

    uint8_t Next() {
        return position < data.size() ? data[position++] : uint8_t{ 0xFF };
    }
};

struct Output {
    Pixels& pixels;
    size_t count = 0;

    void Put(uint8_t value) {
        if (count < kPixels) {
            pixels[count] = value;
        }
        count++;
    }
};

// decrypt_bleeding_animation_data(): one frame's picture, kSourceWidth
// texels a row, from the stream at `position`. The counters are bytes, as in
// the original, so they wrap the same way. Returns the position of the frame
// after it.
size_t DecodeStream(const std::vector<uint8_t>& stream, size_t position, Pixels& pixels) {
    Cursor in{ stream, position };
    Output out{ pixels };
    const uint8_t first = in.Next();
    uint8_t rows = kSourceHeight;

    do {
        uint8_t code = in.Next();
        uint8_t value = 0xFF;
        if (code == 0xFF) {
            // One row as alternating runs, starting with set texels.
            uint8_t filled = 0;
            for (code = in.Next(); code != 0xFF; value ^= 0xFF, code = in.Next()) {
                filled += code;
                for (uint8_t left = code; left > 0; left--) {
                    out.Put(value);
                }
            }
            while (filled++ < kSourceWidth) {
                out.Put(value);
            }
            rows--;
        } else {
            // 1 to 8 identical rows: `set` texels set, then the rest clear.
            const auto set = static_cast<uint8_t>(first + (code & 0x1f));
            uint8_t repeat = (code >> 5) + 1;
            rows -= repeat;
            do {
                for (uint8_t left = set; left > 0; left--) {
                    out.Put(0xFF);
                }
                for (auto left = static_cast<uint8_t>(kSourceWidth - set); left > 0; left--) {
                    out.Put(0);
                }
            } while (--repeat > 0);
        }
    } while (rows > 0);

    return in.position;
}

// bloodImgTranspose(): row-major 80x96 to 96 columns by 80 rows.
void Transpose(const Pixels& source, Pixels& destination) {
    for (int row = 0; row < kSourceHeight; row++) {
        for (int column = 0; column < kSourceWidth; column++) {
            destination[column * kSourceHeight + row] = source[row * kSourceWidth + column];
        }
    }
}

// sub_GAME_7F01D02C() and sub_GAME_7F01CEEC(): a 2x2 box blur of the
// interior, done in place. The backward pass's taps reach over pixels it has
// already written, and its output depends on that.
void BlurForward(Pixels& pixels) {
    size_t at = kSourceHeight + 1;
    for (int i = 1; i < kSourceWidth - 1; i++, at += 2) {
        for (int j = 1; j < kSourceHeight - 1; j++, at++) {
            const int sum = pixels[at + 1] + pixels[at] + pixels[at + kSourceHeight + 1] + pixels[at + kSourceHeight];
            pixels[at] = static_cast<uint8_t>((sum + 2) >> 2);
        }
    }
}

void BlurBackward(Pixels& pixels) {
    size_t at = kSourceHeight + 1;
    for (int i = 1; i < kSourceWidth - 1; i++, at += 2) {
        for (int j = 1; j < kSourceHeight - 1; j++, at++) {
            const int sum = pixels[at - 1] + pixels[at] + pixels[at - kSourceHeight - 1] + pixels[at - kSourceHeight];
            pixels[at] = static_cast<uint8_t>((sum + 2) >> 2);
        }
    }
}

// sub_GAME_7F01CC94(): 8-bit intensity to 4 bits, two texels to a byte, in
// place.
void Pack4(Pixels& pixels) {
    for (size_t i = 0; i < kBloodTextureBytes; i++) {
        pixels[i] = static_cast<uint8_t>((pixels[2 * i] & 0xF0) | (pixels[2 * i + 1] >> 4));
    }
}

} // namespace

BloodAnimation::BloodAnimation(std::vector<uint8_t> stream) : stream_(std::move(stream)) {
}

void BloodAnimation::Start() {
    current_ = 0;
    Decode();
}

bool BloodAnimation::Advance() {
    if (next_ < stream_.size()) {
        current_ = next_;
    }
    return Decode();
}

bool BloodAnimation::Decode() {
    Pixels decoded{};
    next_ = DecodeStream(stream_, current_, decoded);
    // bloodImgIdx flips first.
    current_frame_ = 1 - current_frame_;
    auto& frame = frames_[current_frame_];
    // The texture cache keys on the buffer's address, so the picture it
    // holds from two frames ago has to go.
    TextureCache::Invalidate(frame.data());
    Transpose(decoded, frame);
    BlurForward(frame);
    BlurBackward(frame);
    Pack4(frame);
    return next_ >= stream_.size();
}

} // namespace Frigate::Game::Menu
