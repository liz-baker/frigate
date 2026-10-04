#include "game/save/GameSettings.h"

#include <cstdio>
#include <memory>

#include "game/save/SaveProgress.h"

using namespace Frigate::Game::Save;
using Frigate::Level::ControlConfig;
using Frigate::Level::ScreenSize;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        gFailures++;
    }
}

void TestVolumes() {
    GameSettings settings;
    SaveData save;
    CaptureSettings(settings, save);
    Check(save.musicVol == 0xFF && save.sfxVol == 0xFF, "full volume is stored as 0xFF");

    settings.musicVolume = 0x4000;
    settings.sfxVolume = 0x0080;
    CaptureSettings(settings, save);
    Check(save.musicVol == 0x80 && save.sfxVol == 0x01, "the volumes are stored in the top byte");

    ApplySettings(save, 1, settings);
    Check(settings.musicVolume == 0x4040, "a stored volume spreads back over 15 bits");
    Check(settings.sfxVolume == 0x0080, "so a small one comes back as it was");

    save.musicVol = 0xFF;
    save.sfxVol = 0;
    ApplySettings(save, 1, settings);
    Check(settings.musicVolume == 0x7FFF && settings.sfxVolume == 0, "the ends are full and silent");
}

void TestOptionsRoundTrip() {
    GameSettings settings;
    settings.options.lookInverted = true;
    settings.options.autoAim = false;
    settings.options.aimToggle = true;
    settings.options.sightOnScreen = false;
    settings.options.lookAhead = true;
    settings.options.ammoOnScreen = false;
    settings.options.screen = ScreenSize::Wide;
    settings.options.ratio16x9 = true;
    settings.options.controlType = ControlConfig::Domino;

    SaveData save;
    CaptureSettings(settings, save);
    const uint16_t expected = kOptionInvertLook | kOptionAimControl | kOptionLookAhead | kOptionScreenWide |
                              kOptionScreenRatio | static_cast<uint16_t>(6 << 8);
    Check(save.options == expected, "each option is stored in its bit");

    GameSettings loaded;
    ApplySettings(save, 1, loaded);
    Check(loaded.options.lookInverted && !loaded.options.autoAim && loaded.options.aimToggle &&
              !loaded.options.sightOnScreen && loaded.options.lookAhead && !loaded.options.ammoOnScreen,
          "the toggles come back");
    Check(loaded.options.screen == ScreenSize::Wide && loaded.options.ratio16x9, "and the screen setting");
    Check(loaded.options.controlType == ControlConfig::Domino, "and the control style");

    ApplySettings(save, 2, loaded);
    Check(loaded.options.controlType == ControlConfig::Honey, "a multiplayer game takes the first control style");
    Check(loaded.options.lookInverted, "but the other options");
}

void TestScreenSettings() {
    GameSettings settings;
    SaveData save;
    settings.options.screen = ScreenSize::Cinema;
    CaptureSettings(settings, save);
    Check((save.options & kOptionScreenCinema) != 0 && (save.options & kOptionScreenWide) == 0,
          "cinema is its own bit");
    ApplySettings(save, 1, settings);
    Check(settings.options.screen == ScreenSize::Cinema, "and comes back as cinema");

    save.options = kOptionScreenWide | kOptionScreenCinema;
    ApplySettings(save, 1, settings);
    Check(settings.options.screen == ScreenSize::Cinema, "cinema wins over wide");

    save.options = 0;
    ApplySettings(save, 1, settings);
    Check(settings.options.screen == ScreenSize::Full && !settings.options.ratio16x9, "neither is the full screen");
}

void TestSelectedFolderSettings() {
    auto storage = std::make_shared<MemorySaveStorage>();
    auto manager = std::make_shared<SaveManager>(storage);
    manager->ValidateSaves();
    SaveProgress progress(manager);

    auto settings = std::make_shared<GameSettings>();
    progress.SetSettingsCapture([settings](SaveData& save) { CaptureSettings(*settings, save); });

    progress.SetCurrentFolder(1);
    settings->sfxVolume = 0x2000;
    settings->options.lookInverted = true;
    progress.DeleteCurrentSelectedFolder();

    GameSettings loaded;
    int applied = 0;
    progress.LoadSaveSettingsForSelectedFolder([&](const SaveData& save) {
        ApplySettings(save, 1, loaded);
        applied++;
    });
    Check(applied == 1, "the selected folder's save is applied");
    Check(loaded.sfxVolume == 0x2020 && loaded.options.lookInverted, "with the settings it was last left with");

    // Another run over the same EEPROM keeps them.
    auto second = std::make_shared<SaveManager>(storage);
    second->ValidateSaves();
    SaveProgress again(second);
    again.SetCurrentFolder(1);
    GameSettings reloaded;
    again.LoadSaveSettingsForSelectedFolder([&](const SaveData& save) { ApplySettings(save, 1, reloaded); });
    Check(reloaded.options.lookInverted, "the settings are in the EEPROM");

    GameSettings untouched;
    int calls = 0;
    again.SetCurrentFolder(-1);
    again.LoadSaveSettingsForSelectedFolder([&](const SaveData&) { calls++; });
    Check(calls == 0, "no folder selected applies nothing");
    (void)untouched;
}

} // namespace

int main() {
    TestVolumes();
    TestOptionsRoundTrip();
    TestScreenSettings();
    TestSelectedFolderSettings();
    return gFailures == 0 ? 0 : 1;
}
