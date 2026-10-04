# The player's starting `animFlipFlag`

`struct player.animFlipFlag` (an `s8` at offset 0x5BC) is the mirror flag
the gait animation starts with. The GoldenEye decomp never initialises it, and
the stage memory pool is not cleared between stages, so its first value is
whatever the previous stage left at that address. It decides which way the
sprint animation drifts sideways (a fraction of a degree of path rotation), which
is enough to change where an attract demo's shot lands and so which random
numbers it draws. `StagePoolAnimFlipFlag` (`src/level/LevelInfo.cpp`) holds the
value per stage.

This is a recreation of leftover memory, not game logic. Nothing here affects
live play beyond which way the first walking animation mirrors.

## Where the player struct lands

A stage's pool allocations run in a fixed order with no padding: the MA bank
(`-maNNN` KB), the player struct (0x2A80 bytes), the two gfx buffers, the two
vertex buffers, the texture pool (`-mtNNN` KB, allocated by `set_mt_tex_alloc`),
then everything `texReset` and the stage load allocate. The player struct is
therefore at `-maNNN * 1024` and the flag at that plus 0x5BC.

Attract demos start from the title stage, so the bytes the flag lands on are the
title's. Its row is `-mgfx80 -mvtx20 -mt646 -ma001`:

| Range (from pool start) | Contents                       |
| ----------------------- | ------------------------------ |
| 0x00000 - 0x00400       | MA bank                        |
| 0x00400 - 0x02E80       | player struct                  |
| 0x02E80 - 0x2AE80       | gfx buffers (2 x 80 KB)        |
| 0x2AE80 - 0x34E80       | vertex buffers (2 x 20 KB)     |
| 0x34E80 - 0xD6680       | texture pool (646 KB)          |

| Stage        | Flag offset | In the title's           |
| ------------ | ----------- | ------------------------ |
| Dam          | 0x451BC     | texture pool, +0x1033C   |
| Frigate      | 0x389BC     | texture pool, +0x03B3C   |
| Runway       | 0x4B5BC     | texture pool, +0x1673C   |
| Facility     | 0x285BC     | gfx buffers              |
| Bunker1/Silo | 0x25DBC     | gfx buffers              |
| Train        | 0x325BC     | vertex buffers           |

The gfx and vertex buffers hold whatever display lists and vertices the title's
frames last built, which depends on the exact frame the demo started on. The
three stages in the texture pool read bytes that `texReset` writes when the
title loads, which are fixed by the ROM.

## What `texReset` leaves in the texture pool

`texReset` loads, in order, the textures named by 17 display lists in the global
image table, then the generic image, 6 smoke images and 5 scattered explosions.
Each `texLoad` skips a texture already in the pool, writes an 8-byte header
(the texture number, 6 bytes left stale) and the decoded image after it, then
advances by the returned size. All 43 textures are non-paletted, non-explicit-LOD
textures:

- an image takes its row stride times its height (IA8 rows are padded to 8
  bytes, RGBA16 rows to 4 pixels), rounded up to 8;
- a texture with LOD count N >= 2 adds N-1 shrunk images, each its own
  stride times height, unrounded. Only texture 2136 (IA8 54x54, 3 LODs, 0x1010
  bytes) does;
- LOD-0 textures are not row-swapped, so the pool holds the decoded image as is.

The prefix ends at 0x19768, so the flag offsets inside it (Dam, Frigate,
Runway) are all well inside the part this fixes. `tools/derive_anim_flip_flag.py`
walks the same list, lays the pool out this way, and reads the byte, using the
packer's texture decoder (`tools/o2r_packer/ge_texture.py`, which matches the
decomp's `texChannelsToPixels` for IA8).

| Stage   | Pool offset | Texture                | Row, column | Byte | Flag |
| ------- | ----------- | ---------------------- | ----------- | ---- | ---- |
| Dam     | 0x1033C     | 2177, IA8 64x64 (RLE)  | 48, 28      | 0x90 | set  |
| Frigate | 0x03B3C     | 2100, IA8 56x56 (RLE)  | 21, 36      | 0xFF | set  |
| Runway  | 0x1673C     | 2183, IA8 64x64 (Huffman + blur) | 63, 44 | 0x00 | clear |

The flag is read as a truth value (`if (flip)`) everywhere, and
`bheadFlipAnimation` normalises it to 0 or 1 on the first toggle, so 0x90 and
0xFF both mean set.

How firm each is: Dam's byte sits in a run of non-zero pixels, Frigate's in a run
of 18 bytes of 0xFF, and Runway's in the last row of a texture whose last rows
are all 0x00, so a layout error of a few bytes either way does not change any of
them. Runway's address is 0x14 bytes before the next texture's header, so an
error of that much or more (the unwritten header bytes follow) would. The layout
sizes above come straight from the decomp's return values, so such an error is
unlikely.

## Stages that can't be derived

Facility, Bunker1, Silo and Train read the title's display-list and vertex
buffers, so `StagePoolAnimFlipFlag` leaves them at the default (clear) rather
than fitting a value to a recording. Tracked in
https://github.com/liz-baker/frigate/issues/262.

## Per-demo overrides (fitted, not derived)

Some recordings were evidently made from a different RAM state than the title's
(for instance a demo recorded from a stage reached another way), so no single
per-stage value satisfies all of them. `DemoAnimFlipFlagOverride`
(`src/level/LevelInfo.cpp`) lists the demos whose recording needs a value other
than the stage's. Attract playback uses the override when there is one, else the
stage's `StagePoolAnimFlipFlag`; live starts only ever use the stage value.

These values are **fitted to the recordings**. Nothing in the ROM derives them,
and they are a deliberate departure from a pure port that only affects attract
playback. A demo's value is the one under which a clean majority of its packets
(12 or more, against about none for the other value) match the recorded random
byte.

| Demo | Recording | Value | Stage value | Why fitted |
| ---- | --------- | ----- | ----------- | ---------- |
| 5    | Runway_1  | set   | clear (derived) | The recording needs set, the title's pool byte at Runway's address is 0x00. The layout is byte-exact, so the recording was made from other memory. First mismatch moves from packet 230 to 290. |
| 10   | Silo_2    | set   | clear (not derivable) | Needs set; contradicts Bunker1_1 (clear) at the same address, so no single stage value fits both. 31 to 59. |
| 12   | Frigate_2 | clear | set (derived)   | Needs clear; contradicts Frigate_1 (set) at the same address. 94 to 135. |

Demos left at their stage value:

- Agree with the recording (needs the same value as the stage's): 0 (Dam, set),
  2 (clear), 3 (clear), 7 (clear), 11 (set).
- Indifferent: 6, whose first walking animation never changes a drawn byte.
- Weak signal: 13 probably needs clear, but the evidence is thin, so no override.
- Undetermined: 1, 4, 8, 9. Other, unrelated divergences earlier in each demo hide
  the flag's signal, so there is nothing to fit. Revisit once those are fixed.

## Recordings that disagree

A demo's recorded random byte pins the flag it ran with. Against the derived
values:

- Dam_1 needs set: agrees.
- Frigate_1 needs set (agrees) but Frigate_2 needs clear. Both are the same
  stage with the same address, so one of the two ran from a different pool state
  than the title's (for instance a demo that did not start from the title).
  Frigate_2 is overridden.
- Runway_1 needs set; the derived value is clear. Overridden.
- Bunker1_1 needs clear and Silo_2 needs set, at the same address, so they
  contradict each other for the same reason. Silo_2 is overridden.

The first-mismatch packet of each demo before and after this change is in the
pull request description.
