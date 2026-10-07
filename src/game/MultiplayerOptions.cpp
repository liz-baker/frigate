#include "game/MultiplayerOptions.h"

#include <algorithm>

#include "game/MultiplayerUnlocks.h"

namespace Frigate::Game::MpOptions {

namespace {

constexpr int kMaxWeaponSet = 0xD;
constexpr int kWeaponSetPistols = 1;
constexpr int kWeaponSetGoldenGun = 0xD;
constexpr int kLastTimedLength = static_cast<int>(MpGameLength::TwentyMinutes);
constexpr int kLastPointsLength = static_cast<int>(MpGameLength::TwentyPoints);

// mp_weapon_set_text_table's textID, as a slot of the LMPWEAPONS bank: the
// throwing knives (slot 0xD) come third.
constexpr std::array<int, MultiplayerSettings::kWeaponSets> kWeaponSetText = { 0, 1, 0xD, 2, 3, 4, 5,
                                                                               6, 7, 8,   9, 0xA, 0xB, 0xC };

int GameLengthIndex(const MultiplayerSettings& settings) {
    return static_cast<int>(settings.gameLength);
}

// Game lengths past `last` are not on offer: back to ten minutes.
void LimitGameLength(MultiplayerSettings& settings, int last) {
    if (GameLengthIndex(settings) > last) {
        settings.gameLength = MpGameLength::TenMinutes;
    }
}

void ClampWeaponSet(MultiplayerSettings& settings) {
    settings.weaponSet = std::min(settings.weaponSet, kMaxWeaponSet);
}

} // namespace

RowUnlocks UnlocksFor(MpScenario scenario) {
    RowUnlocks unlocks;
    switch (scenario) {
        case MpScenario::YouOnlyLiveTwice:
            unlocks.gameLength = false;
            break;
        case MpScenario::TheManWithTheGoldenGun:
            unlocks.weaponSelect = false;
            break;
        case MpScenario::LicenceToKill:
            unlocks.handicap = false;
            break;
        default:
            break;
    }
    return unlocks;
}

void ResetForScenario(MultiplayerSettings& settings, MpScenario scenario) {
    settings.scenario = scenario;
    switch (scenario) {
        case MpScenario::Normal:
        case MpScenario::TwoVsTwo:
        case MpScenario::ThreeVsOne:
        case MpScenario::TwoVsOne:
            LimitGameLength(settings, kLastPointsLength);
            ClampWeaponSet(settings);
            break;
        case MpScenario::YouOnlyLiveTwice:
            settings.gameLength = MpGameLength::LastAlive;
            ClampWeaponSet(settings);
            break;
        case MpScenario::TheLivingDaylights:
            LimitGameLength(settings, kLastTimedLength);
            ClampWeaponSet(settings);
            break;
        case MpScenario::TheManWithTheGoldenGun:
            LimitGameLength(settings, kLastPointsLength);
            settings.weaponSet = kWeaponSetGoldenGun;
            break;
        case MpScenario::LicenceToKill:
            LimitGameLength(settings, kLastPointsLength);
            settings.weaponSet = kWeaponSetPistols;
            break;
        default:
            break;
    }
}

void InitForPlayerCount(MultiplayerSettings& settings, int numPlayers) {
    numPlayers = std::max(numPlayers, 2);
    settings.playerCount = numPlayers;

    for (int i = 0; i < numPlayers; i++) {
        const auto player = static_cast<size_t>(i);
        if (settings.character[player] == -1) {
            settings.character[player] = i;
            settings.handicap[player] = MultiplayerSettings::kNormalHandicap;
        }
        if (numPlayers >= 3 && settings.controlStyle[player] >= Level::ControlConfig::Plenty) {
            settings.controlStyle[player] = Level::ControlConfig::Honey;
        }
    }

    const MpPlayerRange range = MultiplayerSettings::ScenarioPlayers(settings.scenario);
    if (range.max < numPlayers || numPlayers < range.min) {
        ResetForScenario(settings, MpScenario::Normal);
    }

    settings.stage =
        static_cast<MpStage>(MultiplayerUnlocks::StageForPlayerCount(static_cast<int>(settings.stage), numPlayers));
}

void SelectGameLength(MultiplayerSettings& settings) {
    const int last = settings.scenario == MpScenario::TheLivingDaylights ? kLastTimedLength : kLastPointsLength;
    settings.gameLength = static_cast<MpGameLength>((GameLengthIndex(settings) + 1) % (last + 1));
}

void IncrementWeaponSet(MultiplayerSettings& settings) {
    settings.weaponSet = (settings.weaponSet + 1) % (kMaxWeaponSet + 1);
}

void AdvanceAimSetting(MultiplayerSettings& settings) {
    settings.aimSetting = (settings.aimSetting + 1) % MultiplayerSettings::kAimRows;
}

std::array<MpAimSetting, kMpMaxPlayers> CopyAimSettingsToPlayerData(const MultiplayerSettings& settings) {
    std::array<MpAimSetting, kMpMaxPlayers> aim;
    aim.fill(settings.Aim());
    return aim;
}

int WeaponSetTextSlot(int weaponSet) {
    return kWeaponSetText[static_cast<size_t>(weaponSet)];
}

} // namespace Frigate::Game::MpOptions
