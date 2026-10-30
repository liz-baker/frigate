#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Frigate::Extract {

// One enumerator of the decomp's SFX_ID or MUSIC_TRACKS enum.
struct AudioName {
    const char* name;
    uint32_t value;
};

namespace Detail {
// Defined in the generated AudioNamesData.cpp (tools/gen_audio_tables.py).
extern const AudioName kSfxNames[];
extern const size_t kSfxNamesCount;
extern const AudioName kMusicNames[];
extern const size_t kMusicNamesCount;
} // namespace Detail

// One name per line, line `n` naming value `n`; a value no enumerator holds is an empty line. This is the payload
// of the `audio/sfx_names` and `audio/music_names` blobs (docs/formats/goldeneye-audio-bank.md).
std::vector<uint8_t> BuildAudioNamesPayload(std::span<const AudioName> names);

} // namespace Frigate::Extract
