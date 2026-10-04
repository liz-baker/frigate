#pragma once

#include <cstdint>

#include "game/save/SaveData.h"
#include "level/PlayerMove.h"

namespace Frigate::Game::Save {

// The music and effects volumes and the player options that a folder's save
// stores (save_data's music_vol, sfx_vol and options): the live values
// fileSaveSettingsForFolder reads and fileLoadSettingsForFolder writes.
struct GameSettings {
    // get_mTrack2Vol() and call_sndGetSfxSlotFirstNaturalVolume(); 0x7FFF is
    // full volume.
    uint16_t musicVolume = 0x7FFF;
    uint16_t sfxVolume = 0x7FFF;
    Level::PlayerOptions options;
};

// fileSaveSettingsForFolder: stores the settings in `save`.
void CaptureSettings(const GameSettings& settings, SaveData& save);

// fileLoadSettingsForFolder, for a folder that has a save: sets the volumes
// and options from `save`. Only a one-player game takes the stored control
// style; with more players it is the first (Honey).
void ApplySettings(const SaveData& save, int playerCount, GameSettings& settings);

} // namespace Frigate::Game::Save
