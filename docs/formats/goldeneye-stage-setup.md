# Stage setup files

Written by `tools/o2r_packer/pack_geometry.py` into the geometry archive as
`obseg/setup/<name>` (`OBLB`), one per row of the decomp's `scripts/filelist.u.csv`
under `assets/obseg/setup/` (for example `obseg/setup/UsetupdamZ`). The payload is
the file after decompression, big-endian. Read by `src/level/StageSetup.cpp`.

Every reference inside the file is a byte offset from its start. The stage
list in `src/level/LevelInfo.cpp` names each stage's file.

## Header

Ten `u32` offsets, in this order:

| Index | Section |
|---|---|
| 0 | path waypoints |
| 1 | waypoint groups |
| 2 | intro |
| 3 | propdefs |
| 4 | patrol paths |
| 5 | AI lists |
| 6 | pads |
| 7 | bound pads |
| 8 | pad names (0 = absent) |
| 9 | bound pad names (0 = absent) |

## Pads

A run of `0x2C`-byte records ended by one whose name offset is 0.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 12 | position (`f32` x3) |
| 0x0C | 12 | up (`f32` x3) |
| 0x18 | 12 | look (`f32` x3) |
| 0x24 | 4 | name offset (a C string in the file) |
| 0x28 | 4 | stand tile pointer; 0 on disk |

Positions are in the stage's file units; the game multiplies them by
`1 / levelscale` when it loads the stage.

## Bound pads

`0x44`-byte records ended the same way: the pad layout above, then a bounding
box of six `f32` (`xmin xmax ymin ymax zmin zmax`) at 0x2C. The position and
box are scaled like pad positions.

## Pad names

A run of `u32` name offsets (0 ends it), one per pad in order. Bound pad names
work the same way.

## Intro section

A run of records, each starting with a `s32` type, ended by type 9. A record's
size follows from its type; an unknown type is 4 bytes long.

| Type | Kind | Size | Fields after the type |
|---|---|---|---|
| 0 | spawn | 0x0C | pad index, demo slot |
| 1 | item | 0x10 | right item, left item (-1 = none), demo slot |
| 2 | ammo | 0x10 | ammo type, amount, demo slot |
| 3 | swirl | 0x20 | flags, three offsets from Bond, scale, duration (each 16.16 fixed point), pad |
| 4 | animation | 0x08 | animation index |
| 5 | cuff | 0x08 | Bond type |
| 6 | camera | 0x28 | x, y, z (`s32`, 100 per centimetre), yaw, pitch (16.16), pad, caption ids (two `u16`), second caption id, 4 bytes that hold a pointer at run time |
| 7 | watch | 0x0C | hours, minutes |
| 8 | credits | 0x08 | offset of the credits table |
| 9 | end | 0x04 | |

A spawn, item or ammo record applies only when its demo slot equals the slot of
the demo being played, or 0 outside a demo (the slot is `slotnum` at 0x94 of
the ramrom file, see `goldeneye-ramrom.md`).
