#pragma once

#include "extract/Error.h"
#include "extract/Extractor.h"
#include "extract/O2rWriter.h"

namespace Frigate::Extract {

// The audio stage: the sound-effect and instrument banks (`audio/sfx`, `audio/instruments`), the music sequences
// (`audio/music`) and the sound and music track names (`audio/sfx_names`, `audio/music_names`).
Error PackAudio(StageContext& context, O2rWriter& writer);

} // namespace Frigate::Extract
