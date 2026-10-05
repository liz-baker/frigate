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

The credits table is an array of 12-byte entries that ends with one whose two
text ids are both 0:

| Offset | Size | Field |
|---|---|---|
| 0 | `u16` | first text id (0x5011 for none) |
| 2 | `u16` | second text id |
| 4 | `s16` | first text position (negative: keep the previous) |
| 6 | `u16` | first text alignment (0 right, 1 left, 2 centre; negative: keep the previous) |
| 8 | `s16` | second text position |
| 10 | `u16` | second text alignment |

A spawn, item or ammo record applies only when its demo slot equals the slot of
the demo being played, or 0 outside a demo (the slot is `slotnum` at 0x88 of
the ramrom file, see `goldeneye-ramrom.md`).

## Propdefs

A run of records ended by one of type 48 (`PROPDEF_END`). Every record starts
with a header word: `u16` extra scale (u8.8), `u8` state, `u8` type. A record's
size follows from its type (`sizepropdef`), in 32-bit words; an unknown type is
1 word long.

| Type | Kind | Words |
|---|---|---|
| 1 | door | 64 |
| 2 | door scale | 2 (then `s32` scale, 16.16 fixed point) |
| 3, 5, 12, 17, 36, 42, 43 | prop, alarm, rack, hat, gas, glass, safe | 32 |
| 4 | key | 33 |
| 6 | CCTV | 0x3B |
| 7 | magazine (ammo crate) | 0x21 |
| 8 | collectable (weapon) | 0x22 |
| 9 | guard | 7 |
| 10 | monitor | 0x40 |
| 11 | multi-monitor | 0x95 |
| 13 | autogun | 0x36 |
| 14, 18 | link, guard attribute | 3 |
| 19, 22, 23, 30, 32, 35, 38 | switch, tag, objective start, photograph, enter room, watch menu text, lock door | 4 |
| 20 | ammo | 0x2D |
| 21 | armour | 0x22 |
| 24, 31, 34 | objective end, null, copy item | 1 |
| 25 to 29 | objective destroy, complete, fail, collect, deposit | 2 |
| 33, 44 | objective deposit in room, safe item | 5 |
| 37 | rename | 10 |
| 39 | vehicle | 0x2C |
| 40 | aircraft | 0x2D |
| 41 | (unnamed) | 1 |
| 45 | tank | 0x38 |
| 46 | camera position | 7 |
| 47 | tinted glass | 37 |

A camera position record (46) holds, after the header, x, y, z (`s32`, 100 per
centimetre), yaw and pitch (16.16 fixed point) and the pad the camera's tile
comes from (`s32`).

The object records (types 1, 3 to 8, 10 to 13, 17, 20, 21, 36, 39, 40, 42, 43,
45 and 47) share these fields after the header:

| Offset | Size | Field |
|---|---|---|
| 0x04 | 2 | model (`PROP_*` index) |
| 0x06 | 2 | pad: below 10000 an index into the pads, 10000 and above one into the bound pads, negative to sit inside the previous object |
| 0x08 | 4 | flags |
| 0x0C | 4 | flags2 |

flags2 bit 4 + difficulty (agent, secret, 00) stops an object loading on that
difficulty; with two or more players bit 20 + the player count does too.

The record sizes are what walks every stage's file to its end record.

A monitor (type 10) carries two more fields after its runtime state, at 0xF4
and 0xF8: `s32` owner offset, the record (relative to this one) whose model the
monitor sits on when its pad is negative and it is not inside another object,
and `s32` owner part, the switch of that model (0 to 2; any other value the
fourth) the monitor is attached to.

A collectable (type 8) or hat (type 17) whose flags include 0x4000 (assigned to
a character) names the character it belongs to in its pad field, as a chrnum.

## Records that refer to other records

These start with the header word and name their targets as `s32` offsets from
their own record index (a target is the record that many places on, or back if
negative):

| Type | Fields after the header |
|---|---|
| 14 link | `0x04` first weapon, `0x08` second weapon: two collectables that pair as a dual-wielded set |
| 19 switch | `0x04` object, `0x08` door: using the object also activates the door |
| 38 lock door | `0x04` door, `0x08` padlock object: the door will not open while the object is intact |
| 44 safe item | `0x04` item, `0x08` safe, `0x0C` door: the item cannot be picked up until the door is open |
| 22 tag | `0x04` `u16` id, `0x06` `s16` offset to the tagged object: a pickup of it is kept in the inventory |
| 37 rename | see below |

A rename record is a text override: `0x04` `s32` offset to the object it names
(0 when it names a weapon id instead), `0x08` `s32` weapon id, then the string
ids `0x0C` first title, `0x10` second title, `0x14` short name, `0x18` long
name and `0x1C` pickup text. The object it names is also marked destroyed.

## Objectives

An objective is an objective start record (type 23) followed by the records
that make up its criteria, up to the objective end record (type 24). The
start record holds `0x04` `s32` objective number (0 to 9), `0x0A` `u16` text
id and `0x0F` `s8` the lowest difficulty the objective applies to. The
criteria records are:

| Type | Fields after the header | Satisfied when |
|---|---|---|
| 25 destroy | `0x04` tag id | the tagged object is destroyed |
| 26 complete condition | `0x04` stage flag bits | any of the bits is set in the AI's objective register |
| 27 fail condition | `0x04` stage flag bits | any of the bits is set, which fails the objective |
| 28 collect | `0x04` tag id | the tagged object is in the inventory (it failing, being destroyed or missing, fails the objective) |
| 29 deposit | `0x04` tag id | the tagged object has left the inventory |
| 30 photograph | `0x04` tag id, `0x08` taken flag | the flag is set; a flag of 0 leaves the objective incomplete while the object is whole and failed once it is not |
| 32 enter room | `0x04` pad, `0x08` entered flag | the flag is set |
| 33 deposit in room | `0x04` weapon id, `0x08` pad, `0x0C` deposited flag | the flag is set |
| 34 copy item | none | the key analyser has copied the GoldenEye key |

The flags are runtime state the game sets in the loaded file. A watch menu
text record (type 35) holds `0x04` the briefing page and `0x0A` `u16` text id.
