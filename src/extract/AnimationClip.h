#pragma once

#include "extract/Extractor.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frigate::Extract {

// The animation clip packer (tools/o2r_packer/ge_animation.py): every clip as `animations/<name>`
// (docs/formats/goldeneye-animation-clip-resource.md), read from the ROM's animationtable_data and
// animationtable_entries files. Objects archive.
Error PackAnimationClips(StageContext& context, O2rWriter& writer);

// One clip's payload. `table` is the animation data file with the first word of each clip header cleared;
// `headerOffset` is the clip's header in it and `entryOffset` its frames' place in `entries`.
Error EncodeAnimationClip(std::span<const uint8_t> table, std::span<const uint8_t> entries, uint32_t headerOffset,
                          uint32_t entryOffset, std::vector<uint8_t>& out);

} // namespace Frigate::Extract
