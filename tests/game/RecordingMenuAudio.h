#pragma once

#include <vector>

#include "game/menu/MenuAudio.h"

namespace Frigate::Game::Menu {

// A MenuAudio that records what the screens play instead of making a sound.
class RecordingMenuAudio final : public MenuAudio {
  public:
    std::vector<int> sfx;
    int folderMusic = 0;
    int folderMusicFull = 0;

    void PlaySfx(AudioDriver::SfxId id) override {
        sfx.push_back(id);
    }
    void PlayFolderMusic() override {
        folderMusic++;
    }
    void PlayFolderMusicAtFullVolume() override {
        folderMusicFull++;
    }
};

} // namespace Frigate::Game::Menu
