#include "AudioSink.h"

#include "ship/audio/Audio.h"
#include "ship/audio/AudioPlayer.h"

namespace Frigate::Audio {

bool PushPcm(const std::shared_ptr<Ship::Audio>& audio, const int16_t* samples, size_t sampleCount,
             int32_t maxBufferedFrames) {
    if (!audio) {
        return false;
    }

    auto player = audio->GetAudioPlayer();
    if (!player || !player->IsInitialized()) {
        return false;
    }

    int32_t channels = player->GetNumOutputChannels();
    if (channels <= 0 || sampleCount % static_cast<size_t>(channels) != 0) {
        return false;
    }

    if (player->Buffered() >= maxBufferedFrames) {
        return false;
    }

    player->Play(reinterpret_cast<const uint8_t*>(samples), sampleCount * sizeof(int16_t));
    return true;
}

} // namespace Frigate::Audio
