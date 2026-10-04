#include "game/menu/MenuAudio.h"

#include "MusicPlayer.h"
#include "game/Sound.h"

namespace Frigate::Game::Menu {

namespace {

class GameMenuAudio final : public MenuAudio {
  public:
    void PlaySfx(AudioDriver::SfxId sfx) override {
        Sound::PlaySfx(sfx);
    }

    void PlayFolderMusic() override {
        Sound::MusicTrackPlay(0, AudioDriver::kMusicFolders);
    }

    void PlayFolderMusicAtFullVolume() override {
        Sound::ApplyVolumeAllSfxSlot(AudioDriver::kMusicVolumeMax);
        Sound::MusicTrackApplySeqpVol(0, AudioDriver::kMusicVolumeMax);
        Sound::SetMusicTrackFade(0, AudioDriver::kMusicFadeUnset);
        Sound::MusicTrackPlay(0, AudioDriver::kMusicFolders);
    }
};

MenuAudio* gInstalled = nullptr;

} // namespace

MenuAudio& Audio() {
    static GameMenuAudio game;
    return gInstalled != nullptr ? *gInstalled : game;
}

void SetMenuAudio(MenuAudio* audio) {
    gInstalled = audio;
}

} // namespace Frigate::Game::Menu
