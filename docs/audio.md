# Audio

How sound gets from the ROM to the speakers, and how to check it.

## Pipeline

1. `just pack_archives` writes `archives/goldeneye_audio.o2r`: the sound-effect
   bank (`audio/sfx`), the instrument bank (`audio/instruments`) and the
   sound-effect names (`audio/sfx_names`). Layouts:
   [`formats/goldeneye-audio-bank.md`](formats/goldeneye-audio-bank.md).
2. `src/resource/` loads `audio/sfx` as an `AudioBankResource` (`GEAB`).
3. `src/audio/AudioSynth.cpp` is the synthesis driver: GoldenEye's voice pool
   (a decoder, resampler and envelope mixer per voice, mixed onto a main bus
   and a reverb bus) generating each audio frame as microcode operations that
   `AudioUcode` runs.
4. `src/audio/SoundPlayer.cpp` is the game's sound-effect player: sounds start,
   change and stop through timed events, one `SoundState` per sound.
5. `src/audio/AudioEngine.cpp` configures the synth as the game does (24
   voices, 22 kHz, the game's reverb table) and renders fixed 736-frame
   stereo frames.
6. `PcmResampler` converts the synth's 22 kHz to the device's rate and
   `AudioSink` hands it to libultraship's audio player.

## Checking a sound

```sh
just play_sound rocket_launch    # a name, any case, `_SFX` optional
just play_sound 1                # or a number
just play_sound audio/sfx/GLASS_SHATTERING_SFX
```

This opens no window: it plays the sound to the end of its envelope and the
reverb tail, and exits. A sound that holds until released (a loop) is let go
after six seconds. The names are the decomp's `SFX_ID` enum; an unknown name
lists the ones that contain it.

`SDL_AUDIODRIVER=disk just play_sound <name>` writes the output as raw
signed 16-bit stereo PCM to `sdlaudio.raw` instead of a sound device, which is
how `tests/audio/sound_test_e2e.py` checks the whole path without a ROM.

## Not done yet

- The instrument bank and music sequences are packed but not played, and the
  game's sound calls are not wired in: see #241 and #242.
