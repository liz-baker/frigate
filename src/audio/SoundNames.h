#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The sound-effect names the packer writes to `audio/sfx_names` and the music
// track names it writes to `audio/music_names` (one per line, line `n` naming
// entry `n`; docs/formats/goldeneye-audio-bank.md), and lookup of one from
// what a person types.
namespace Frigate::Audio {

enum class NameKind {
    kSfx,
    kMusic,
};

class SoundNames {
  public:
    explicit SoundNames(std::string_view text, NameKind kind = NameKind::kSfx);

    // The entry a query names: a number (decimal or 0x hex), a name in any
    // case with or without its `_SFX` suffix (`M_` prefix for music), or
    // either of those after an `audio/sfx/` (`audio/music/`) path prefix.
    std::optional<int> Find(std::string_view query) const;

    // Names containing `query`, for a "did you mean" message.
    std::vector<std::string> Suggest(std::string_view query, size_t limit = 8) const;

    // The name of sound `index`, or an empty string.
    const std::string& Name(int index) const;
    int Count() const {
        return static_cast<int>(mNames.size());
    }

  private:
    NameKind mKind;
    std::vector<std::string> mNames;
};

} // namespace Frigate::Audio
