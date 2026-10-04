#pragma once

#include <vector>

#include "SfxIds.h"
#include "game/menu/FileSelectModel.h"
#include "game/menu/FolderTabs.h"

namespace Frigate::Game::Menu {

// The SFX_ID each cue the front-end models raise plays (sndPlaySfx on the
// game's sound-effect bank).
constexpr AudioDriver::SfxId SfxFor(FolderSfx cue) {
    switch (cue) {
        case FolderSfx::DoorMetalClose:
            return AudioDriver::kSfxDoorMetalClose;
        case FolderSfx::DoorMetalClose2:
            return AudioDriver::kSfxDoorMetalClose2;
        case FolderSfx::PaperTurn:
            return AudioDriver::kSfxPaperTurn;
    }
    return AudioDriver::kSfxNothing;
}

constexpr AudioDriver::SfxId SfxFor(FileSelectSfx cue) {
    switch (cue) {
        case FileSelectSfx::OptionClick2:
            return AudioDriver::kSfxOptionClick2;
        case FileSelectSfx::GunM60AmmGun3:
            return AudioDriver::kSfxGunM60ammgun3;
        case FileSelectSfx::PaperTurn:
            return AudioDriver::kSfxPaperTurn;
        case FileSelectSfx::CopyFile:
            return AudioDriver::kSfxCopyFile;
        case FileSelectSfx::DoorLock:
            return AudioDriver::kSfxDoorLock;
    }
    return AudioDriver::kSfxNothing;
}

// Where the front-end screens send their sounds and music. The game's
// implementation plays through Sound; tests install a recording one.
class MenuAudio {
  public:
    virtual ~MenuAudio() = default;
    virtual void PlaySfx(AudioDriver::SfxId sfx) = 0;
    // musicTrack1Play(M_FOLDERS).
    virtual void PlayFolderMusic() = 0;
    // The mission-failed screen's start of it: sndApplyVolumeAllSfxSlot(0x7fff),
    // musicTrack1ApplySeqpVol(0x7fff), g_musicXTrack1Fade = MUSIC_FADESTATE_UNSET,
    // then musicTrack1Play(M_FOLDERS).
    virtual void PlayFolderMusicAtFullVolume() = 0;
};

// The installed MenuAudio, or the game's own when none is. SetMenuAudio(nullptr)
// restores the default; the installed object must outlive its use.
MenuAudio& Audio();
void SetMenuAudio(MenuAudio* audio);

template <typename Cue> void PlayCues(const std::vector<Cue>& cues) {
    for (const Cue cue : cues) {
        Audio().PlaySfx(SfxFor(cue));
    }
}

} // namespace Frigate::Game::Menu
