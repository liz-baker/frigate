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

Things to know when wiring it into the game (#242):

- Nothing runs on its own: the synth runs the players inside
  `AudioEngine::RenderFrame`, so music advances only as frames are rendered.
  `FadeTick()` is the game's per-frame call (60 Hz in the original), not the
  engine's.
- `TrackPlay` has the original's "wait for the old track to stop" built in as
  a deferred start: if the slot's player is still releasing its notes, the new
  track loads during a later `RenderFrame` (`Update()` finishes it) instead of
  spinning. `CurrentTrack` reads back the new number at once.
- No call is thread safe; if the game and the audio callback are on different
  threads, serialize them.
- `SetBootswitchSound(true)` makes every music call a no-op, as the game's
  sound-disabled flag does.

## Not done yet

- The reverb's low-pass filter (`aPoleFilter`) is a pass-through in
  `AudioUcode`, so reverb tails are brighter than the game's: #243.
- The game's sound and music calls are not wired in: #242.
- The music players run without tremolo and vibrato oscillators, as the game
  configures them, so instruments that name one play without it.
