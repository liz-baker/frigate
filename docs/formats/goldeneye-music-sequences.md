# Music sequences file and `GEMS` resource

GoldenEye's music is a table of compact MIDI sequences, played by the sequence
player on the instruments from `audio/instruments` (a `GEAB` bank; see
`goldeneye-audio-bank.md`). The ROM stores the table and the sequences in one
file, `assets/music/music.sbk` in the decomp's `scripts/filelist.u.csv`, which
the decomp's linker script calls the music sample table. Every sequence is
compressed. `tools/o2r_packer/ge_audio.py` decompresses them and
`pack_audio.py` writes the result to `audio/music`, type `GEMS`, version 0,
big-endian, after the header in `o2r-resource-header.md`.

## The `.sbk` file

Everything is big-endian.

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u16` | sequence count (63 in the US ROM) |
| `0x02` | `u16` | unused, zero |
| `0x04` | entry list | one entry per sequence |

Entry (8 bytes), indexed by track number (the decomp's `MUSIC_TRACKS` enum;
track 0 is `M_NONE`, a stub):

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | offset of the compressed sequence from the start of the file |
| `0x04` | `u16` | length after decompression |
| `0x06` | `u16` | length in the file |

A compressed sequence is the same raw-DEFLATE container the texture and model
files use (a `0x1172` or `0x1173` header, then the stream; see
`goldeneye-texture-bitstream.md`).

The game adds the file's ROM address to every entry's offset when it loads the
table (`musicSeqFileNew`) and treats an address below `0x10000` as "no data for
this track".

## A decompressed sequence

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` x16 | offset of each track's first event, or 0 for no track |
| `0x40` | `u32` | ticks per quarter note |

Each track is a run of events. An event is a variable-length delta time (7 bits
per byte, high bit set on all but the last) followed by the event itself. The
next event played is the soonest of all the tracks' next events.

Event bytes:

| Bytes | Event |
|---|---|
| status (`0x80`-`0xEF`), data | a MIDI channel message: one data byte for program change and channel pressure, two for the rest. A first byte without the high bit reuses the last status |
| note on, key, velocity, duration | the same, followed by a variable-length duration in ticks: the note's note-off is generated from it instead of stored |
| `FF 51` b1 b2 b3 | tempo: microseconds per quarter note, 24 bits |
| `FF 2F` | end of track; the sequence ends with its last track |
| `FF 2E` b1 b2 | loop start (the two bytes are not used) |
| `FF 2D` count remaining dist | loop end: `count` and `remaining` are one byte each, `dist` is a `u32` giving how far back from the end of this event to jump. `remaining` of `0xFF` loops forever; otherwise it is decremented in place on each jump, and at 0 it is reset to `count` and the loop falls through |
| `FE` hi lo len | a back-reference: read the next `len` bytes from `hi:lo` bytes before the start of this code, then carry on after the code. `FE FE` is a literal `FE` |

Back-references apply to every byte read from a track, delta times included. A
loop end rewrites its own `remaining` byte, so a sequence is played from a
private copy.

## The `GEMS` payload

| Size | Type | Field |
|---|---|---|
| 4 | `u32` | sequence count |
| 4 | `u32` | ROM address of the `.sbk` file (what the game adds to the entries' offsets) |
| 12 each | | entries: `u32` offset in the file, `u16` decompressed length, `u16` compressed length, `u32` where the decompressed sequence starts in the data below |
| 4 | `u32` | length of the data |
| | | the decompressed sequences, one after another |

The packer checks that each sequence decompresses to its stated length and has
a header that points inside it. Reader: `src/audio/MusicSequences.cpp`.

## Track names

`audio/music_names` is a `Blob` of the `MUSIC_TRACKS` names, one per line,
line `n` naming track `n`, written like `audio/sfx_names` in
`goldeneye-audio-bank.md`. `pack_audio.py` reads them from the decomp's
`src/bondconstants.h`.
