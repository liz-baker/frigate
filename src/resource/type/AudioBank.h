#pragma once

#include "AudioBank.h"
#include "ship/resource/Resource.h"

// A packed audio bank ("GEAB"), from the packer's `audio/sfx` or
// `audio/instruments` resource. Payload: docs/formats/goldeneye-audio-bank.md.
namespace Frigate {

class AudioBankResource final : public Ship::Resource<Audio::AudioBank> {
  public:
    using Resource::Resource;

    AudioBankResource();

    Audio::AudioBank* GetPointer() override;
    size_t GetPointerSize() override;

    Audio::AudioBank Data;
};

} // namespace Frigate
