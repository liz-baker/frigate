#include "game/MissionMusic.h"

#include "game/Sound.h"
#include "level/LevelInfo.h"

namespace Frigate::Game {

namespace {

using Level::LevelId;
using namespace AudioDriver;

// The track numbers the state machine plays by number.
static_assert(kMusicWatch == 0x18 && kMusicMpDeath == 0x3A);

constexpr int kNone = -1;

// music_setup_entries: per stage, its theme, background sound and tense track.
struct MusicSetup {
    LevelId stage;
    int theme;
    int background;
    int tense;
};

constexpr MusicSetup kMusicSetup[] = {
    { LevelId::Bunker1, kMusicBunker1, kNone, kMusicBunker1X },
    { LevelId::Silo, kMusicSilo, kNone, kMusicSiloX },
    { LevelId::Statue, kMusicStatue, kNone, kMusicStatuePart },
    { LevelId::Control, kMusicControl, kNone, kMusicElevatorControl },
    { LevelId::Archives, kMusicArchives, kNone, kMusicArchivesX },
    { LevelId::Train, kMusicTrain, kNone, kMusicTrainX },
    { LevelId::Frigate, kMusicFrigate, kNone, kMusicEndSomething },
    { LevelId::Bunker2, kMusicBunker2, kNone, kMusicBunker2X },
    { LevelId::Aztec, kMusicAztec, kNone, kMusicAztecX },
    { LevelId::Streets, kMusicStreets, kNone, kMusicStreetsX },
    { LevelId::Depot, kMusicDepot, kNone, kMusicDepotX },
    { LevelId::Egypt, kMusicEgyptian, kNone, kMusicEgyptX },
    { LevelId::Dam, kMusicDam, kNone, kMusicWind },
    { LevelId::Facility, kMusicFacility, kNone, kMusicFacilityX },
    { LevelId::Runway, kMusicRunway, kNone, kMusicRunwayPlane },
    { LevelId::Surface, kMusicSurface1, kNone, kMusicWind },
    { LevelId::Jungle, kMusicJungle, kNone, kMusicJungleX },
    { LevelId::Caverns, kMusicWaterCaverns, kNone, kMusicElevatorWc },
    { LevelId::Citadel, kMusicCitadel, kNone, kNone },
    { LevelId::Cradle, kMusicCradle, kNone, kMusicCradleX },
    { LevelId::Sho, kMusicSurface2, kNone, kNone },
    { LevelId::Surface2, kMusicSurface2, kMusicWind, kMusicSurface2End },
    { LevelId::Cuba, kMusicCuba, kNone, kNone },
};

// random_tracks: what a stage with no theme of its own plays.
constexpr int kRandomTracks[] = {
    kMusicTrain,           kMusicDepot,         kMusicMpTheme,    kMusicCitadel,   kMusicFacility,
    kMusicControl,         kMusicDam,           kMusicFrigate,    kMusicArchives,  kMusicSilo,
    kMusicMpTheme2,        kMusicStreets,       kMusicBunker1,    kMusicBunker2,   kMusicStatue,
    kMusicElevatorControl, kMusicCradle,        kMusicElevatorWc, kMusicEgyptian,  kMusicAztec,
    kMusicWaterCaverns,    kMusicSurface2,      kMusicTrainX,     kMusicFacilityX, kMusicDepotX,
    kMusicControlX,        kMusicWaterCavernsX, kMusicDamX,       kMusicFrigateX,  kMusicArchivesX,
    kMusicSiloX,           kMusicStreetsX,      kMusicBunker1X,   kMusicBunker2X,  kMusicJungleX,
    kMusicStatueX,         kMusicAztecX,        kMusicEgyptX,     kMusicCradleX,   kMusicRunway,
    kMusicMpTheme3,        kMusicRunwayX,       kMusicSurface1,
};

const MusicSetup* SetupFor(int stage) {
    for (const MusicSetup& entry : kMusicSetup) {
        if (static_cast<int>(entry.stage) == stage) {
            return &entry;
        }
    }
    return nullptr;
}

// The music slots (musicTrack1 to 3) by the roles the state machine gives them.
constexpr int kSlotTheme = 0;
constexpr int kSlotTense = 1;
constexpr int kSlotBackground = 2;

// The volume the slot's player runs at while a track plays on it: the options'
// music volume for the first two, the effects' for the background sound
// (sub_GAME_7F0C0BF0 and sub_GAME_7F0C0C10).
uint16_t MusicVolume() {
    return Sound::GetMTrack2Vol();
}

uint16_t BackgroundVolume() {
    return Sound::GetSfxSlotFirstNaturalVolume();
}

// musicTrackNApplySeqpVol, `g_musicXTrackNFade = 0` and musicTrackNPlay: a
// track starts at `volume`, cancelling a fade that was running.
void StartTrack(int slot, uint16_t volume, int track) {
    Sound::MusicTrackApplySeqpVol(slot, volume);
    Sound::SetMusicTrackFade(slot, kMusicFadeUnset);
    Sound::MusicTrackPlay(slot, track);
}

void StopAll() {
    for (int slot = 0; slot < kMusicSlotCount; slot++) {
        Sound::MusicTrackStop(slot);
    }
}

// The framerate the AI's slot durations are counted in (NTSC).
constexpr int kMusicFramerate = 60;

} // namespace

int MissionMusic::ThemeFor(int stage) {
    if (const MusicSetup* entry = SetupFor(stage); entry != nullptr && entry->theme != kNone) {
        return entry->theme;
    }
    constexpr uint32_t kCount = sizeof(kRandomTracks) / sizeof(kRandomTracks[0]);
    return kRandomTracks[mRandom() % kCount];
}

int MissionMusic::BackgroundTrackFor(int stage) {
    const MusicSetup* entry = SetupFor(stage);
    return entry != nullptr ? entry->background : kNone;
}

int MissionMusic::TenseTrackFor(int stage) {
    const MusicSetup* entry = SetupFor(stage);
    return entry != nullptr ? entry->tense : kNone;
}

void MissionMusic::StartStage(int stage) {
    StopAll();
    mState = MissionState::Stopped;
    mStageMusicId = stage;
    SetState(BackgroundTrackFor(mStageMusicId) < 0 ? MissionState::Theme : MissionState::ThemeBackground);
}

void MissionMusic::SetState(MissionState state) {
    const MissionState old = mState;
    mState = state;

    // Each state's handler lists the changes it knows; one it does not list
    // carries on into the next state's, as the original's switch falls through.
    for (int from = static_cast<int>(old); from <= static_cast<int>(MissionState::Dead); from++) {
        if (Handle(static_cast<MissionState>(from), state)) {
            return;
        }
    }
    // A change the handlers of every later state refuse: the original spins
    // here forever, which does nothing useful to keep.
}

bool MissionMusic::Handle(MissionState from, MissionState to) {
    using S = MissionState;
    const int stage = mStageMusicId;

    // Starting a track on `slot` at the options' music volume.
    const auto playMusic = [](int slot, int track) { StartTrack(slot, MusicVolume(), track); };

    switch (from) {
        case S::Stopped:
            switch (to) {
                case S::Stopped:
                    return true;
                case S::Theme:
                    playMusic(kSlotTheme, ThemeFor(stage));
                    return true;
                case S::ThemeBackground:
                    playMusic(kSlotTheme, ThemeFor(stage));
                    StartTrack(kSlotBackground, BackgroundVolume(), BackgroundTrackFor(stage));
                    return true;
                default:
                    return false;
            }
        case S::Theme:
            switch (to) {
                case S::Stopped:
                    StopAll();
                    return true;
                case S::Tense:
                    playMusic(kSlotTense, TenseTrackFor(stage));
                    Sound::MusicTrackFadeOut(kSlotTheme, 0.5f);
                    return true;
                case S::Watch:
                    playMusic(kSlotTense, kMusicWatch);
                    Sound::MusicTrackFadeOut(kSlotTheme, 0.5f);
                    return true;
                case S::Dead:
                    Sound::MusicTrackFadeOut(kSlotTheme, 0.02f);
                    playMusic(kSlotTense, kMusicMpDeath);
                    return true;
                default:
                    return false;
            }
        case S::Tense:
            switch (to) {
                case S::Stopped:
                    StopAll();
                    return true;
                case S::Theme:
                    Sound::MusicTrackFadeIn(kSlotTheme, 0.5f, MusicVolume());
                    Sound::MusicTrackFadeOut(kSlotTense, 0.5f);
                    return true;
                case S::Tense:
                    return true;
                case S::Watch:
                    playMusic(kSlotTense, kMusicWatch);
                    return true;
                default:
                    return false;
            }
        case S::Watch:
            switch (to) {
                case S::Stopped:
                    StopAll();
                    return true;
                case S::Theme:
                    Sound::MusicTrackFadeIn(kSlotTheme, 1.0f, MusicVolume());
                    Sound::MusicTrackFadeOut(kSlotTense, 1.0f);
                    return true;
                case S::Tense:
                    playMusic(kSlotTense, TenseTrackFor(stage));
                    return true;
                case S::ThemeBackground:
                    Sound::MusicTrackFadeIn(kSlotTheme, 1.0f, MusicVolume());
                    Sound::MusicTrackFadeIn(kSlotBackground, 1.0f, BackgroundVolume());
                    Sound::MusicTrackFadeOut(kSlotTense, 1.0f);
                    return true;
                case S::TenseBackground:
                    Sound::MusicTrackFadeIn(kSlotBackground, 1.0f, BackgroundVolume());
                    playMusic(kSlotTense, TenseTrackFor(stage));
                    return true;
                default:
                    return false;
            }
        case S::ThemeBackground:
            switch (to) {
                case S::Stopped:
                    StopAll();
                    return true;
                case S::Watch:
                    playMusic(kSlotTense, kMusicWatch);
                    Sound::MusicTrackFadeOut(kSlotTheme, 0.5f);
                    Sound::MusicTrackFadeOut(kSlotBackground, 0.5f);
                    return true;
                case S::TenseBackground:
                    playMusic(kSlotTense, TenseTrackFor(stage));
                    Sound::MusicTrackFadeOut(kSlotTheme, 0.5f);
                    return true;
                default:
                    return false;
            }
        case S::TenseBackground:
            switch (to) {
                case S::Stopped:
                    StopAll();
                    return true;
                case S::Watch:
                    playMusic(kSlotTense, kMusicWatch);
                    Sound::MusicTrackFadeOut(kSlotBackground, 0.5f);
                    return true;
                case S::ThemeBackground:
                    Sound::MusicTrackFadeIn(kSlotTheme, 0.5f, MusicVolume());
                    Sound::MusicTrackFadeOut(kSlotTense, 0.5f);
                    return true;
                default:
                    return false;
            }
        case S::Dead:
            switch (to) {
                case S::Stopped:
                    StopAll();
                    return true;
                case S::Theme:
                    Sound::MusicTrackFadeIn(kSlotTheme, 2.0f, MusicVolume());
                    Sound::MusicTrackFadeOut(kSlotTense, 2.0f);
                    return true;
                case S::Dead:
                    Sound::SetMusicTrackFade(kSlotTense, kMusicFadeUnset);
                    Sound::MusicTrackPlay(kSlotTense, kMusicMpDeath);
                    return true;
                default:
                    return false;
            }
    }
    return false;
}

void MissionMusic::EnterWatch() {
    mSavedState = mState;
    SetState(MissionState::Watch);
}

void MissionMusic::LeaveWatch() {
    SetState(mSavedState);
}

void MissionMusic::RaiseTense() {
    SetState(BackgroundTrackFor(mStageMusicId) < 0 ? MissionState::Tense : MissionState::TenseBackground);
}

void MissionMusic::LowerTense() {
    SetState(BackgroundTrackFor(mStageMusicId) < 0 ? MissionState::Theme : MissionState::ThemeBackground);
}

void MissionMusic::PlaySlot(int slot, int stoppedSeconds, int totalSeconds) {
    if (slot < 0 || slot >= static_cast<int>(mSlotActive.size())) {
        return;
    }
    if (!mSlotActive[static_cast<size_t>(slot)]) {
        mSlotActive[static_cast<size_t>(slot)] = true;
        mSlotStopped[static_cast<size_t>(slot)] = stoppedSeconds * kMusicFramerate;
        mSlotTotal[static_cast<size_t>(slot)] = totalSeconds * kMusicFramerate;
    }
}

void MissionMusic::StopSlot(int slot) {
    if (slot >= 0) {
        if (slot < static_cast<int>(mSlotActive.size())) {
            mSlotActive[static_cast<size_t>(slot)] = false;
        }
        return;
    }
    ResetSlots();
}

void MissionMusic::ResetSlots() {
    mSlotActive.fill(false);
    mSlotStopped.fill(0);
    mSlotTotal.fill(0);
}

void MissionMusic::TickSlots(int clockTimer) {
    bool anyRunning = false;
    for (size_t i = 0; i < mSlotActive.size(); i++) {
        if (!mSlotActive[i] && mSlotStopped[i] <= 0) {
            continue;
        }
        mSlotStopped[i] = mSlotStopped[i] >= clockTimer ? mSlotStopped[i] - clockTimer : 0;

        if (mSlotTotal[i] != 0) {
            mSlotTotal[i] = mSlotTotal[i] >= clockTimer ? mSlotTotal[i] - clockTimer : 0;
            if (mSlotTotal[i] != 0) {
                if (mSlotActive[i] || mSlotStopped[i] != 0) {
                    anyRunning = true;
                }
            } else {
                mSlotActive[i] = false;
            }
        }
    }

    if (clockTimer != 0) {
        if (mState == MissionState::Tense || mState == MissionState::TenseBackground) {
            if (!anyRunning) {
                LowerTense();
            }
        } else if (anyRunning) {
            RaiseTense();
        }
    }
}

} // namespace Frigate::Game
