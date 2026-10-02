#pragma once

#include "MusicSequences.h"
#include "ship/resource/Resource.h"

// The packed music sequence table ("GEMS"), from the packer's `audio/music`
// resource. Payload: docs/formats/goldeneye-music-sequences.md.
namespace Frigate {

class MusicSequencesResource final : public Ship::Resource<Audio::MusicSequenceTable> {
  public:
    using Resource::Resource;

    MusicSequencesResource();

    Audio::MusicSequenceTable* GetPointer() override;
    size_t GetPointerSize() override;

    Audio::MusicSequenceTable Data;
};

} // namespace Frigate
