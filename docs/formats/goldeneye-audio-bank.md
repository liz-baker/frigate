# Audio bank file and `GEAB` resource

GoldenEye keeps its instruments and sound effects in two libultra bank files,
each a pair: a `.ctl` (the bank file: instruments, sounds, envelopes, ADPCM
books, loops) and a `.tbl` (the sample table: raw wavetable data the `.ctl`
points into). The ROM stores both uncompressed; the decomp's
`scripts/filelist.u.csv` lists them as `assets/music/sfx.ctl`/`.tbl` and
`assets/music/instruments.ctl`/`.tbl`. `tools/o2r_packer/ge_audio.py` decodes a
pair and `pack_audio.py` writes it to `audio/sfx` and `audio/instruments`,
type `GEAB`, version 0, big-endian, after the header in
`o2r-resource-header.md`.

## The `.ctl` file

Everything is big-endian, and every pointer is a byte offset from the start of
the file (the game adds the file's address to each at load). A zero offset
where a structure is optional means none.

File header:

| Offset | Type | Field |
|---|---|---|
| `0x00` | `s16` | revision: `0x4231` (`'B1'`) |
| `0x02` | `s16` | bank count |
| `0x04` | `u32` list | bank offsets |

Bank:

| Offset | Type | Field |
|---|---|---|
| `0x00` | `s16` | instrument count |
| `0x02` | `u8` | flags (the game sets this at load to mark the bank patched; zero in the file) |
| `0x03` | `u8` | padding |
| `0x04` | `s32` | sample rate |
| `0x08` | `u32` | percussion instrument offset, or 0 |
| `0x0C` | `u32` list | instrument offsets, one per instrument |

Instrument:

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u8` | volume |
| `0x01` | `u8` | pan (0 hard left, 127 hard right) |
| `0x02` | `u8` | priority |
| `0x03` | `u8` | flags |
| `0x04` | `u8` x4 | tremolo type, rate, depth, delay |
| `0x08` | `u8` x4 | vibrato type, rate, depth, delay |
| `0x0C` | `s16` | pitch bend range, in cents |
| `0x0E` | `s16` | sound count |
| `0x10` | `u32` list | sound offsets, one per sound |

Sound:

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | envelope offset |
| `0x04` | `u32` | key map offset |
| `0x08` | `u32` | wavetable offset |
| `0x0C` | `u8` | sample pan |
| `0x0D` | `u8` | sample volume |
| `0x0E` | `u8` | flags |

Envelope: `s32` attack, decay and release times in microseconds (a decay time
of -1 marks a sound that holds until released), then `u8` attack volume and
`u8` decay volume.

Key map: `u8` velocity min, velocity max, key min, key max, key base, then
`s8` detune. The sound-effect player reads two fields as parameters rather
than as a key range: key min's low six bits are the sound's group, and key
max's low nibble is its effects mix; the high nibble carries flags
(`snd.h`'s `SoundFlags` and the player's own).

Wavetable:

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | offset of the sample data in the `.tbl` |
| `0x04` | `s32` | length of the data, in bytes |
| `0x08` | `u8` | type: 0 ADPCM, 1 raw 16-bit |
| `0x09` | `u8` | flags (zero in the file) |
| `0x0C` | `u32` | loop offset, or 0 |
| `0x10` | `u32` | ADPCM book offset (ADPCM only) |

ADPCM book: `s32` order, `s32` predictor count, then `order * predictors * 8`
`s16` words. Loop: `u32` start, end and count, then 16 `s16` of decoder state
(a raw wavetable's loop is the three words only).

## The `GEAB` payload

The same structures with the pointers replaced by indices into flat tables, so
the runtime does no patching. A structure shared in the file (one envelope or
book serving several sounds) is stored once. An index of `0xFFFFFFFF` means
none.

| Size | Type | Field |
|---|---|---|
| 32 | `u32` x8 | counts: envelopes, key maps, books, loops, wavetables, sounds, instruments, banks |
| 16 each | | envelopes: `s32` attack, decay, release, `u8` attack volume, `u8` decay volume, 2 bytes padding |
| 8 each | | key maps: the six bytes above, 2 bytes padding |
| variable | | books: `u32` order, `u32` predictors, then the `s16` words |
| 44 each | | loops: `u32` start, end, count, then 16 `s16` state words |
| 20 each | | wavetables: `u32` sample offset, `u32` length, `u8` type, `u8` flags, 2 bytes padding, `u32` loop index, `u32` book index |
| 16 each | | sounds: `u32` envelope, key map and wavetable index, `u8` pan, `u8` volume, `u8` flags, 1 byte padding |
| variable | | instruments: the 16-byte header above (sound count as `u16`), then a `u32` sound index per sound |
| variable | | banks: `u32` sample rate, `u32` instrument count, `u32` percussion index, then a `u32` instrument index per slot (`0xFFFFFFFF` for an empty slot) |
| 4 | `u32` | sample table length |
| | | the `.tbl` bytes, unchanged |

Wavetable sample offsets stay relative to the sample table at the end of the
same resource. Reader: `src/audio/AudioBank.cpp`.

## Sound-effect numbering and names

The game plays a sound effect by its number, and the sound-effect player reads
it from the first instrument of the `audio/sfx` bank counted from one: number
`n` is that instrument's sound `n - 1` in the payload above. Number 0 is "no
sound". A sound's key map can name a following sound of the same effect:
`velocityMin` plus the top two bits of `keyMin` (shifted up two) is that sound's
number, and `velocityMax` is the delay, in 33.3 ms steps, before it starts.

`audio/sfx_names` is a `Blob` of the sound-effect names in the decomp's
`SFX_ID` enum, one per line, line `n` naming sound `n` (an empty line where no
name holds that value). `pack_audio.py` reads them from the decomp's
`src/bondconstants.h` when it packs.
