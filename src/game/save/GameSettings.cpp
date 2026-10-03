#include "game/save/GameSettings.h"

namespace Frigate::Game::Save {

void CaptureSettings(const GameSettings& settings, SaveData& save) {
    using Level::ScreenSize;
    const Level::PlayerOptions& options = settings.options;

    uint16_t bits = 0;
    save.musicVol = static_cast<uint8_t>(settings.musicVolume >> 7);
    save.sfxVol = static_cast<uint8_t>(settings.sfxVolume >> 7);

    if (options.lookInverted) {
        bits = kOptionInvertLook;
    }
    if (options.autoAim) {
        bits |= kOptionAutoAim;
    }
    if (options.aimToggle) {
        bits |= kOptionAimControl;
    }
    if (options.sightOnScreen) {
        bits |= kOptionSightOnScreen;
    }
    if (options.lookAhead) {
        bits |= kOptionLookAhead;
    }
    if (options.ammoOnScreen) {
        bits |= kOptionDisplayAmmo;
    }
    if (options.screen == ScreenSize::Wide) {
        bits |= kOptionScreenWide;
    } else if (options.screen == ScreenSize::Cinema) {
        bits |= kOptionScreenCinema;
    }
    if (options.ratio16x9) {
        bits |= kOptionScreenRatio;
    }

    const auto controlType = static_cast<uint16_t>(
        static_cast<uint16_t>(static_cast<uint16_t>(options.controlType) << 8) & kOptionControlType);
    save.options = static_cast<uint16_t>(bits | controlType);
}

void ApplySettings(const SaveData& save, int playerCount, GameSettings& settings) {
    settings.musicVolume = static_cast<uint16_t>((save.musicVol << 7) | (save.musicVol >> 1));
    settings.sfxVolume = static_cast<uint16_t>((save.sfxVol << 7) | (save.sfxVol >> 1));

    settings.options = Level::PlayerOptions::FromSaveBits(save.options);
    if (playerCount != 1) {
        settings.options.controlType = Level::ControlConfig::Honey;
    }
}

} // namespace Frigate::Game::Save
