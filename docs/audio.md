# Audio

How sound gets from the ROM to the speakers, and how to check it.

## Pipeline

1. `just pack_archives` writes `archives/goldeneye_audio.o2r`: the sound-effect
   bank (`audio/sfx`), the instrument bank (`audio/instruments`), the music
   sequence table (`audio/music`) and the names of the sound effects and
   music tracks (`audio/sfx_names`, `audio/music_names`). Layouts:
   [`formats/goldeneye-audio-bank.md`](formats/goldeneye-audio-bank.md),
   [`formats/goldeneye-music-sequences.md`](formats/goldeneye-music-sequences.md).
2. `src/resource/` loads the banks as `AudioBankResource` (`GEAB`) and the
   sequence table as `MusicSequencesResource` (`GEMS`).
3. `src/audio/AudioSynth.cpp` is the synthesis driver: GoldenEye's voice pool
   (a decoder, resampler and envelope mixer per voice, mixed onto a main bus
   and a reverb bus) generating each audio frame as microcode operations that
   `AudioUcode` runs.
4. `src/audio/SoundPlayer.cpp` is the game's sound-effect player: sounds start,
   change and stop through timed events, one `SoundState` per sound.
5. `src/audio/CompactSeqPlayer.cpp` and `CompactSeq.cpp` are libultra's compact
   sequence player (the `alCSP*` calls) and the sequence it reads, and
   `src/audio/MusicPlayer.cpp` is the game's music code (`music.c`) on top:
   three players, one per music slot.
6. `src/audio/AudioEngine.cpp` configures the synth as the game does (24
   voices, 22 kHz, the game's reverb table), owns the sound-effect and music
   players, and renders fixed 736-frame stereo frames.
7. `PcmResampler` converts the synth's 22 kHz to the device's rate and
   `AudioSink` hands it to libultraship's audio player.

## Checking a sound or a track

```sh
just play_sound rocket_launch    # a name, any case, `_SFX` optional
just play_sound 1                # or a number
just play_sound audio/sfx/GLASS_SHATTERING_SFX

just play_sound M_INTRO          # a music track: plays in a loop until Ctrl-C
just play_sound intro            # `M_` is optional, any case
just play_sound audio/music/2    # a track by number
just play_sound M_INTRO --play-seconds 20   # stop after 20 seconds
```

This opens no window. A sound effect plays to the end of its envelope and the
reverb tail, then exits; one that holds until released (a loop) is let go
after six seconds. The names are the decomp's `SFX_ID` and `MUSIC_TRACKS`
enums; an unknown name lists the ones that contain it. A bare number is
always a sound effect. A track that ends by itself (a few do, the rest loop
inside the sequence) is started again after its tail.

`SDL_AUDIODRIVER=disk just play_sound <name>` writes the output as raw
signed 16-bit stereo PCM to `sdlaudio.raw` instead of a sound device, which is
how `tests/audio/sound_test_e2e.py` checks the whole path without a ROM.

## The music API (`src/audio/MusicPlayer.h`)

`AudioEngine` creates a `MusicPlayer` when it is given the instrument bank and
the sequence table (`MusicAssets`); `engine.Music()` returns it. Its calls
mirror `music.c`'s, with the `musicTrack1`/`2`/`3` families taking a slot
number (0 to 2) instead:

| Call | `music.c` |
|---|---|
| `TrackPlay(slot, track)` | `musicTrackNPlay` |
| `TrackStop(slot)` | `musicTrackNStop` |
| `TrackGetVolume(slot)` | `musicTrackNGetVolume` |
| `TrackApplySeqpVol(slot, volume)` | `musicTrackNApplySeqpVol` |
| `TrackSaveCurrentVolumeAsTrackDefault(slot)` | `musicTrackNSaveCurrentVolumeAsTrackDefault` |
| `TrackFadeOut(slot, seconds)` | `musicTrackNFadeOut` |
| `TrackFadeIn(slot, seconds, volume)` | `musicTrackNFadeIn` |
| `FadeTick()` | `musicFadeTick` |

`track` is a `MusicTrack` (the decomp's `MUSIC_TRACKS` numbers). Volumes run 0
to `kMusicVolumeMax` (0x7FFF); the player's own volume is the slot volume
scaled by the track's default volume. Per-slot state is readable through
`CurrentTrack(slot)`, `FadeState(slot)`, `SeqState(slot)` (stopped, playing or
stopping) and `IsPlaying(slot)`.

Things to know when wiring it into the game:

- Nothing runs on its own: the synth runs the players inside
  `AudioEngine::RenderFrame`, so music advances only as frames are rendered.
  `FadeTick()` is the game's retrace-rate call (60 Hz in the original), not the
  engine's; `Sound::RenderFrame` makes it (below).
- `TrackPlay` has the original's "wait for the old track to stop" built in as
  a deferred start: if the slot's player is still releasing its notes, the new
  track loads during a later `RenderFrame` (`Update()` finishes it) instead of
  spinning. `CurrentTrack` reads back the new number at once.
- No call is thread safe; the game calls go through `Sound` (below), whose
  lock is shared with the thread that renders frames.
- `SetBootswitchSound(true)` makes every music call a no-op, as the game's
  sound-disabled flag does.

## The game's sound calls (`src/game/Sound.h`)

The game calls the sound layer through `Frigate::Game::Sound`, never the
engine, with the decomp's function names in PascalCase (`sndPlaySfx` is
`Sound::PlaySfx`, `musicTrack2FadeOut` is `Sound::MusicTrackFadeOut(1, ...)`,
the track number becoming a slot 0 to 2). Sounds are named by the
`AudioDriver::SfxId` constants in `src/audio/SfxIds.h`
(`kSfxRocketLaunch`; generated from the decomp's `SFX_ID` by
`tools/gen_sfx_ids.py`) and music by `MusicTrack`.

| Call | Decomp |
|---|---|
| `PlaySfx(sound, &handle)` | `sndPlaySfx(g_musicSfxBufferPtr, sound, pending)` |
| `GetPlayingState(state)`, `Deactivate(state)` | `sndGetPlayingState`, `sndDeactivate` |
| `DeactivateAllSfxByFlag(flag)`, `...Flag1/11/3()` | `sndDeactivateAllSfxByFlag*` |
| `CreatePostEvent`, `SetVolume(state, v)` | `sndCreatePostEvent`, the same with `AL_SNDP_VOL_EVT` |
| `GetSfxSlotFirstNaturalVolume`, `ApplyVolumeAllSfxSlot`, `SetScalerApplyVolumeAllSfxSlot` | the `snd*` calls of those names |
| `MusicTrackPlay/Stop/GetVolume/ApplySeqpVol/...(slot, ...)` | `musicTrackNPlay` and the rest |
| `MusicTrackFade(slot)`, `SetMusicTrackFade(slot, v)` | reads and writes of `g_musicXTrackNFade` |
| `GetMTrack2Vol`, `SetMTrack2Vol` | options.c's `get_mTrack2Vol`, `set_mTrack2Vol` |
| `VolumeForDistance`, `VolumeAtPosition(pos, low, high)`, `...Default` | `sub_GAME_7F0537B8`, `sub_GAME_7F053894`, `sub_GAME_7F0539B8`, `sub_GAME_7F0539E4` |
| `PostVolumeAtPosition(state, pos, low, high)`, `...Default` | `chrobjSndCreatePostEvent`, `chrobjSndCreatePostEventDefault` |

Things to know when calling them:

- **Handles.** The `pending` argument is a `SoundState*` variable the caller
  keeps (`projectile->sounds[0]`, a door's open sound): `PlaySfx` stores the new
  state in it and the player resets it to null when the sound ends. It must
  outlive the sound; one that is about to go away while its sound plays on
  calls `Sound::DetachHandle(&handle)` first.
- **Positions.** `VolumeAtPosition` measures to the nearest listener, which
  the stage supplies through `SetListenerSource` (the player's prop position;
  none outside a stage, which is silence). Pass the same positions the decomp
  does (`prop->pos`, or `runtime_pos` where it uses that).
- **No audio.** With no engine installed (no audio archive, no output device)
  every call is a no-op and `PlaySfx` returns null, as with the original's
  disabled-sound flag; the game still runs.
- **Threads.** `main` starts `SoundOutput` (`src/game/SoundOutput.cpp`), which
  builds the engine from the loaded archives, installs it, and runs a thread
  that renders `Sound::RenderFrame` frames to the output as it drains them, so
  music carries on while a stage loads. Every `Sound` call and `RenderFrame`
  share one lock.
- **Fades.** `RenderFrame` makes the original's retrace-rate `musicFadeTick`
  calls itself, twice a frame (`kFadeTicksPerAudioFrame`).

The pieces built on them:

- `MissionMusic` (`src/game/MissionMusic.h`, `mp_music.c` and
  `music_0D2720.c`): the stage's theme, tense track, watch music and
  background sound on the three slots, owned by `Stage` (`Stage::Music()`).
  `Stage::Load` starts it, `Stage::Unload` stops it, `TickSlots` is
  `reset_all_music_slots`.
- `AiSoundChannels` (`src/game/AiSoundChannels.h`, chrai.c's `sfx_related`):
  the AI's eight sound channels, behind the stage AI's `Sfx*` commands. `Stage`
  owns one; `Stage::Unload` stops its sounds.

### Where the game plays sound

Each site mirrors its decomp counterpart, with the handles a sound's owner
keeps as `Game::Sound::Handle` (`src/game/SoundHandle.h`; it detaches itself
when the owner goes away, and a copy of the owner does not share the sound).

- Front end: legal screen, Nintendo logo, Rareware logo, gun barrel, cast and
  file select music; the watch's static, beeps and watch music.
- Weapons (`GunTick`, `GunLauncher`, `PlayerWeapons`): shots, empty clicks,
  reload and pickup sounds, the magnet's hum, knife swishes, thrown objects and
  rocket launches; `Casings`, `Explosions`, `GlassShards`.
- Hits: `HitSounds` (the per-surface tables in `TextureHits`) for bullets on
  props, characters, glass and the world, including the autogun's.
- Characters: sneezes, hit yelps, ricochets off hats, deaths, the gunshot
  sound, and the stop when a character is freed.
- Doors, the alarm, toxic gas (hiss and cough), pickups, the tank (engine,
  exit fade, crushing) and vehicle and aircraft engines.
- Mission music (`MissionMusic`), the death music, and the stage AI's
  sound and music commands (`AiSoundChannels`).

`Stage::StopAudioForPause` runs while the controls are locked and stops the
alarm, gas, the hands', tank and engine sounds. `Stage::Unload` stops the same
sounds and the music, and `Stage::Load` resets the AI channels.

## Not done yet

- The reverb's low-pass filter (`aPoleFilter`) is a pass-through in
  `AudioUcode`, so reverb tails are brighter than the game's: #243.
- Screen shake from explosions and the explosion-entry side effects, and the
  death camera's mute: #136.
- The music players run without tremolo and vibrato oscillators, as the game
  configures them, so instruments that name one play without it.
