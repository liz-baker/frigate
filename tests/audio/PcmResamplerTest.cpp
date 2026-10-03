#include "PcmResampler.h"
#include "SoundNames.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Frigate::Audio;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

std::vector<int16_t> Ramp(int frames) {
    std::vector<int16_t> in;
    for (int i = 0; i < frames; i++) {
        in.push_back(int16_t((i + 1) * 100));
        in.push_back(int16_t(-(i + 1) * 100));
    }
    return in;
}

void TestDoublingInterpolatesBetweenFrames() {
    PcmResampler resampler(22050, 44100);
    const std::vector<int16_t> in = Ramp(8);
    std::vector<int16_t> out;
    resampler.Process(in.data(), 8, &out);
    // The first output frame is the (silent) frame before the chunk.
    Check(out.size() == 16 * 2, "twice the frames out as in");
    Check(out[0] == 0 && out[1] == 0, "starts at the previous frame");
    Check(out[2] == 50 && out[3] == -50, "halfway between the previous frame and the first");
    Check(out[4] == 100 && out[5] == -100, "then the first frame itself");
    Check(out[6] == 150, "then halfway to the second");
}

void TestChunksJoinWithoutASeam() {
    PcmResampler whole(22048, 44100);
    PcmResampler split(22048, 44100);
    const std::vector<int16_t> in = Ramp(200);
    std::vector<int16_t> a;
    std::vector<int16_t> b;
    whole.Process(in.data(), 200, &a);
    split.Process(in.data(), 73, &b);
    split.Process(in.data() + 73 * 2, 127, &b);
    Check(a == b, "resampling in pieces gives the same samples as resampling at once");
}

void TestOutputCountTracksTheRatio() {
    PcmResampler resampler(22048, 44100);
    std::vector<int16_t> frame(736 * 2, 1000);
    std::vector<int16_t> out;
    size_t total = 0;
    for (int i = 0; i < 100; i++) {
        out.clear();
        resampler.Process(frame.data(), 736, &out);
        total += out.size() / 2;
    }
    const double expected = 73600.0 * 44100 / 22048;
    Check(std::abs(double(total) - expected) < 3, "the output length follows the rate ratio");
}

void TestConstantSignalStaysConstant() {
    PcmResampler resampler(22048, 48000);
    std::vector<int16_t> frame(736 * 2, 1234);
    std::vector<int16_t> out;
    resampler.Process(frame.data(), 736, &out);
    resampler.Process(frame.data(), 736, &out);
    bool constant = true;
    // The first frames blend in the silence before the chunk.
    for (size_t i = 8; i < out.size(); i++) {
        constant = constant && out[i] == 1234;
    }
    Check(constant, "a steady level survives resampling");
}

void TestNamesFindByNumberNameAndPath() {
    const SoundNames names("NOTHING_SFX\nROCKET_LAUNCH_SFX\nGLASS_SHATTERING_SFX\n\nLAST_SFX");
    Check(names.Count() == 5, "five lines");
    Check(names.Find("2") == 2, "decimal number");
    Check(names.Find("0x4") == 4, "hex number");
    Check(names.Find("ROCKET_LAUNCH_SFX") == 1, "exact name");
    Check(names.Find("rocket_launch_sfx") == 1, "any case");
    Check(names.Find("rocket_launch") == 1, "without the suffix");
    Check(names.Find("audio/sfx/glass_shattering") == 2, "after a path prefix");
    Check(names.Find("audio/sfx/3") == 3, "a number after a path prefix");
    Check(!names.Find("nonsense").has_value(), "an unknown name");
    Check(!names.Find("12abc").has_value(), "a malformed number");
    Check(!names.Find("").has_value(), "an empty query");
    Check(names.Name(1) == "ROCKET_LAUNCH_SFX" && names.Name(3).empty() && names.Name(99).empty(), "Name");
}

void TestMusicNamesFindByNameAndPath() {
    const SoundNames names("M_NONE\nM_SHORT_SOLO_DEATH\nM_INTRO", NameKind::kMusic);
    Check(names.Find("M_INTRO") == 2, "exact name");
    Check(names.Find("intro") == 2, "without the M_ prefix");
    Check(names.Find("audio/music/m_short_solo_death") == 1, "after a path prefix");
    Check(names.Find("audio/music/2") == 2, "a number after a path prefix");
    Check(!names.Find("audio/sfx/intro").has_value(), "the sound-effect prefix is not a music path");
}

void TestNamesIgnoreTrailingNuls() {
    const std::string text("A_SFX\nB_SFX\0\0\0", 14);
    const SoundNames names(text);
    Check(names.Count() == 2 && names.Name(1) == "B_SFX", "a blob's padding is not part of the last name");
}

void TestSuggestionsMatchSubstrings() {
    const SoundNames names("NOTHING_SFX\nROCKET_LAUNCH_SFX\nROCKET_HIT_SFX\nGLASS_SFX");
    const auto matches = names.Suggest("rocket");
    Check(matches.size() == 2 && matches[0] == "ROCKET_LAUNCH_SFX", "names containing the text");
    Check(names.Suggest("rocket", 1).size() == 1, "limited");
}

} // namespace

int main() {
    TestDoublingInterpolatesBetweenFrames();
    TestChunksJoinWithoutASeam();
    TestOutputCountTracksTheRatio();
    TestConstantSignalStaysConstant();
    TestNamesFindByNumberNameAndPath();
    TestMusicNamesFindByNameAndPath();
    TestNamesIgnoreTrailingNuls();
    TestSuggestionsMatchSubstrings();
    if (gFailures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", gFailures);
        return 1;
    }
    std::puts("PcmResamplerTest passed");
    return 0;
}
