#include "AudioOutput.h"

#include <chrono>
#include <thread>

#include "AudioSink.h"
#include "MasterVolume.h"
#include "ship/audio/Audio.h"
#include "ship/audio/AudioPlayer.h"

namespace Frigate::Audio {

AudioOutput::AudioOutput(const std::shared_ptr<Ship::Audio>& audio, const std::shared_ptr<Ship::AudioPlayer>& player,
                         int inputRate)
    : mAudio(audio), mPlayer(player), mResampler(inputRate, player->GetSampleRate()),
      mDesired(player->GetDesiredBuffered()) {
}

bool AudioOutput::WantsFrame() {
    if (!mPending.empty()) {
        if (PushPcm(mAudio, mPending.data(), mPending.size())) {
            mPending.clear();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }
    if (mPlayer->Buffered() >= mDesired) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return false;
    }
    return true;
}

void AudioOutput::Submit(const int16_t* frame, size_t frames) {
    const int percent = MasterVolumePercent();
    if (percent >= 100) {
        mResampler.Process(frame, frames, &mPending);
        return;
    }
    mScaled.assign(frame, frame + frames * 2);
    ApplyVolume(mScaled.data(), mScaled.size(), percent);
    mResampler.Process(mScaled.data(), frames, &mPending);
}

void AudioOutput::Drain() {
    while (!mPending.empty() && !PushPcm(mAudio, mPending.data(), mPending.size())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    while (mPlayer->Buffered() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace Frigate::Audio
