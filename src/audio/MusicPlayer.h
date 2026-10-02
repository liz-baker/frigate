#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "AudioSynth.h"
#include "CompactSeq.h"
#include "CompactSeqPlayer.h"
#include "MusicSequences.h"

// GoldenEye's music (music.c): three sequence players ("tracks" 1 to 3, here
// slots 0 to 2) that play compact sequences from the Rare sequence table on
// the synth, each with its own volume, fade and current track number. A level
// normally plays its theme on slot 0 and uses the others for the watch menu's
// music and cross-fades.
//
// Not thread safe: whoever renders audio frames must also be the one calling
// these, or serialize them.
namespace Frigate::AudioDriver {

constexpr int kMusicSlotCount = 3;
// Tracks in the sequence table, not counting M_NONE.
constexpr int kNumMusicTracks = 63;
constexpr uint16_t kMusicVolumeMax = 0x7FFF;
// What the fade-in calls take for "the volume saved when the fade-out began".
constexpr uint16_t kMusicVolumeSaved = 0xFFFF;

// MUSIC_TRACKS: the track numbers the game plays music by.
enum MusicTrack : int {
    kMusicNone,
    kMusicShortSoloDeath,
    kMusicIntro,
    kMusicTrain,
    kMusicDepot,
    kMusicMpTheme,
    kMusicCitadel,
    kMusicFacility,
    kMusicControl,
    kMusicDam,
    kMusicFrigate,
    kMusicArchives,
    kMusicSilo,
    kMusicMpTheme2,
    kMusicStreets,
    kMusicBunker1,
    kMusicBunker2,
    kMusicStatue,
    kMusicElevatorControl,
    kMusicCradle,
    kMusicUnk,
    kMusicElevatorWc,
    kMusicEgyptian,
    kMusicFolders,
    kMusicWatch,
    kMusicAztec,
    kMusicWaterCaverns,
    kMusicDeathSolo,
    kMusicSurface2,
    kMusicTrainX,
    kMusicUnk2,
    kMusicFacilityX,
    kMusicDepotX,
    kMusicControlX,
    kMusicWaterCavernsX,
    kMusicDamX,
    kMusicFrigateX,
    kMusicArchivesX,
    kMusicSiloX,
    kMusicEgyptianX,
    kMusicStreetsX,
    kMusicBunker1X,
    kMusicBunker2X,
    kMusicJungleX,
    kMusicIntroSwoosh,
    kMusicStatueX,
    kMusicAztecX,
    kMusicEgyptX,
    kMusicCradleX,
    kMusicCuba,
    kMusicRunway,
    kMusicRunwayPlane,
    kMusicMpTheme3,
    kMusicWind,
    kMusicGuitarGliss,
    kMusicJungle,
    kMusicRunwayX,
    kMusicSurface1,
    kMusicMpDeath,
    kMusicSurface2X,
    kMusicSurface2End,
    kMusicStatuePart,
    kMusicEndSomething,
};

// Fade states (MUSIC_FADESTATE).
enum MusicFadeState : int32_t {
    kMusicFadeOut = -1,
    kMusicFadeUnset = 0,
    kMusicFadeIn = 1,
};

struct MusicPlayerConfig {
    int maxVoices = 0x10;
    int maxEvents = 0x40;
    int maxChannels = 16;
    // Frames per second the fade times are counted in.
    float fadeFramerate = 60.0f;
};

class MusicPlayer {
  public:
    // musicSeqPlayerInit: creates the three players on `synth` (which must
    // outlive this), with programs taken from the first bank of `instruments`
    // (which must too), and the tracks from `sequences`.
    MusicPlayer(Synth* synth, const LinkedBank& instruments, Audio::MusicSequenceTable sequences,
                const MusicPlayerConfig& config = MusicPlayerConfig());
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;

    // musicTrackNPlay: stops what slot `slot` is playing and starts `track`
    // (a MusicTrack) from the beginning at the slot's current volume. A track
    // with no data in the table plays M_SHORT_SOLO_DEATH instead. Returns
    // false, and does nothing, for a slot or track number that does not exist.
    //
    // The original waits for the old track's voices to be released before
    // loading the new one. That wait needs audio frames to run, so here a play
    // that finds its player still stopping is finished by Update() once it has
    // stopped; the track number reads back as `track` straight away.
    bool TrackPlay(int slot, int track);
    // musicTrackNStop: stops the slot's track and clears its track number and
    // fade, whatever state it was in.
    void TrackStop(int slot);
    // musicTrackNGetVolume: the slot's volume, 0 to kMusicVolumeMax.
    uint16_t TrackGetVolume(int slot) const;
    // musicTrackNApplySeqpVol: sets the slot's volume. The player runs at the
    // volume scaled by the current track's default volume.
    void TrackApplySeqpVol(int slot, uint16_t volume);
    // musicTrackNSaveCurrentVolumeAsTrackDefault: the volume the slot is at
    // becomes the current track's default volume (shared by all slots).
    void TrackSaveCurrentVolumeAsTrackDefault(int slot);
    // musicTrackNFadeOut: starts a fade to silence over `seconds`, unless a
    // fade-out is already running. The player is stopped when it reaches 0.
    void TrackFadeOut(int slot, float seconds);
    // musicTrackNFadeIn: starts the player and a fade up to `volume` over
    // `seconds` (kMusicVolumeSaved for the volume the last fade-out began
    // at), unless a fade-in is already running.
    void TrackFadeIn(int slot, float seconds, uint16_t volume);
    // musicFadeTick: advances every slot's fade by one frame. Call once per
    // game frame (60 Hz).
    // TODO(https://github.com/liz-baker/frigate/issues/242): call it from the
    // game loop.
    void FadeTick();

    // Finishes TrackPlay calls waiting on a player to stop.
    void Update();

    // The slot's current track number, or 0 (g_musicXTrackNCurrentTrackNum).
    int CurrentTrack(int slot) const;
    // MusicFadeState of the slot (g_musicXTrackNFade).
    int32_t FadeState(int slot) const;
    // The slot's player state (alCSPGetState): SeqPlayState.
    int32_t SeqState(int slot) const;
    // Whether the slot's player is playing (a sequence still has events).
    bool IsPlaying(int slot) const {
        return SeqState(slot) == kSeqPlaying;
    }
    // The default volume of a track, 0 to kMusicVolumeMax.
    int16_t TrackDefaultVolume(int track) const;

    // Music calls do nothing while set (g_sndBootswitchSound).
    void SetBootswitchSound(bool disabled) {
        mBootswitchSound = disabled;
    }

    CompactSeqPlayer& Player(int slot) {
        return *mSlots[static_cast<size_t>(slot)].player;
    }
    const Audio::MusicSequenceTable& Sequences() const {
        return mSequences;
    }

  private:
    struct Slot {
        std::unique_ptr<CompactSeqPlayer> player;
        CompactSeq seq;
        // The decompressed sequence the player reads, and writes loop counters to.
        std::vector<uint8_t> seqData;
        int32_t currentTrack = 0;
        uint16_t volume = kMusicVolumeMax;
        int32_t fade = kMusicFadeUnset;
        uint16_t preFadeSavedVolume = 0;
        int16_t fadeToVolume = 0;
        int32_t fadeRemainingFrames = 0;
        // A TrackPlay waiting for the player to stop.
        bool playPending = false;
    };

    bool ValidSlot(int slot) const {
        return slot >= 0 && slot < kMusicSlotCount;
    }
    // The part of musicTrackNPlay after waiting for the player to stop.
    void LoadAndPlay(int slot);
    void FadeTickSlot(Slot& slot);

    Audio::MusicSequenceTable mSequences;
    MusicPlayerConfig mConfig;
    std::array<Slot, kMusicSlotCount> mSlots;
    // g_musicDefaultTrackVolume: the entries for each track, M_NONE, and the
    // control sequence.
    std::array<int16_t, kNumMusicTracks + 2> mDefaultVolume;
    bool mBootswitchSound = false;
};

} // namespace Frigate::AudioDriver
