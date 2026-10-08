#include "game/menu/BloodAnimation.h"

#include <cstdio>

#include "gfx/TextureCache.h"

using namespace Frigate::Game::Menu;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

bool All(const uint8_t* texture, uint8_t value) {
    for (size_t i = 0; i < kBloodTextureBytes; i++) {
        if (texture[i] != value) {
            return false;
        }
    }
    return true;
}

// A frame of 96 rows, each `set` texels set out of 80, in the repeated-row
// form: the first byte plus the code's low five bits give the set count.
std::vector<uint8_t> RepeatedRows(uint8_t first, uint8_t low) {
    std::vector<uint8_t> frame{ first };
    for (int i = 0; i < 12; i++) {
        frame.push_back(0xE0 | low); // eight rows
    }
    return frame;
}

// The same in the alternating-run form: one row per record.
std::vector<uint8_t> RunRows(std::vector<uint8_t> runs) {
    std::vector<uint8_t> frame{ 0 };
    for (int i = 0; i < 96; i++) {
        frame.push_back(0xFF);
        frame.insert(frame.end(), runs.begin(), runs.end());
        frame.push_back(0xFF);
    }
    return frame;
}

} // namespace

int main() {
    {
        BloodAnimation animation(RepeatedRows(64, 16));
        animation.Start();
        Check(All(animation.Texture(), 0xFF), "a fully set picture packs to all-set nibbles");
    }
    {
        BloodAnimation animation(RepeatedRows(0, 0));
        animation.Start();
        Check(All(animation.Texture(), 0x00), "a fully clear picture packs to all-clear nibbles");
    }
    {
        BloodAnimation animation(RunRows({ 80 }));
        animation.Start();
        Check(All(animation.Texture(), 0xFF), "one run of 80 set texels fills each row");
    }
    {
        // Set for 40 texels of each row, clear for the rest: the columns of the
        // transposed frame's first half are set, the second half clear.
        BloodAnimation animation(RunRows({ 40, 40 }));
        animation.Start();
        const uint8_t* texture = animation.Texture();
        Check(texture[0] == 0xFF, "the picture's first rows come out set");
        Check(texture[kBloodTextureBytes - 1] == 0x00, "the picture's last rows come out clear");
    }
    {
        // Two frames: Start decodes the first, Advance the second and reports
        // the stream used up.
        std::vector<uint8_t> stream = RepeatedRows(64, 16);
        const std::vector<uint8_t> second = RepeatedRows(0, 0);
        stream.insert(stream.end(), second.begin(), second.end());
        BloodAnimation animation(stream);
        animation.Start();
        Check(All(animation.Texture(), 0xFF), "the first frame is decoded first");
        Check(animation.Advance(), "the last frame reports the stream exhausted");
        Check(All(animation.Texture(), 0x00), "the second frame follows the first");
        Check(animation.Advance(), "advancing past the end stays exhausted");
        Check(All(animation.Texture(), 0x00), "advancing past the end repeats the last frame");
    }
    {
        BloodAnimation animation({ 1, 2, 3 });
        animation.Start();
        Check(animation.Advance(), "a truncated stream decodes without running off its end");
    }

    {
        // Each decode goes to the buffer the last one was not in, and the
        // texture cache forgets the buffer before it is rewritten, since it
        // keys on the address.
        std::vector<const void*> forgotten;
        Frigate::TextureCache::Invalidator() = [&](const void* address) { forgotten.push_back(address); };
        std::vector<uint8_t> stream = RepeatedRows(64, 16);
        const std::vector<uint8_t> second = RepeatedRows(0, 0);
        stream.insert(stream.end(), second.begin(), second.end());
        stream.insert(stream.end(), second.begin(), second.end());
        BloodAnimation animation(stream);
        animation.Start();
        const uint8_t* first = animation.Texture();
        animation.Advance();
        const uint8_t* next = animation.Texture();
        Check(first != next, "a frame is not decoded over the one just drawn");
        animation.Advance();
        Check(animation.Texture() == first, "the two buffers take turns");
        Check(forgotten.size() == 3 && forgotten[0] == first && forgotten[1] == next && forgotten[2] == first,
              "each decode invalidates the buffer it writes");
        Frigate::TextureCache::Invalidator() = nullptr;
    }

    return gFailures == 0 ? 0 : 1;
}
