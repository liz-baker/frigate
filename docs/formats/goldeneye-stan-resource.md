# Stan file and `GEST` resource

A stage's stan file holds its collision tiles: a triangulated floor per room,
with the tiles linked to their neighbours. `tools/o2r_packer/ge_stan.py`
decodes it and `pack_geometry.py` writes it to `obseg/stan/<name>` (the name
`LevelEntry::stanName` holds), type `GEST`, version 0, big-endian, after the
header in `o2r-resource-header.md`.

## The stan file

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | zero |
| `0x04` | `u32` | file offset of the first tile |
| `0x08` | `u32` list | region start offsets (debug region stepping only), ended by a zero word |
| first tile | | tiles, ended by a tile whose id and room words are zero |

The list of region offsets is not read outside a debug build and is not
packed.

A tile is an 8-byte header and its points:

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u24` | name: bit 23 is the type (`p` = 0, `q` = 1), bits 8-22 the number, then a 5-bit file letter and a 3-bit sub-triangle in the low byte (`p1234a2`) |
| `0x03` | `u8` | room |
| `0x04` | `u16` | high nibble: special kind (`0` normal, `1` kneeling, `3` ladder); the other three nibbles are the tile's RGB |
| `0x06` | `u16` | high nibble: point count (3 to 10); the other three nibbles are the indices of the three points whose plane gives the tile's height |
| `0x08` | | points, 8 bytes each: `s16` x, y, z and a `u16` link |

Edge `i` runs from point `i` to point `(i + 1) % count`, and point `i`'s link
names the tile across that edge. A link is `(target offset - (first tile
offset - 0x80)) / 8`; a link below 16 (`link >> 4 == 0`) means no neighbour.
Coordinates are in stan units: world units times the stage's scale
(`LevelEntry::scale`).

Citadel's stan file is in a beta layout (float points, tiles named by
pointer) that the game's stan code cannot read; the packer leaves it an
opaque `OBLB` blob.

## The `GEST` payload

| Size | Type | Field |
|---|---|---|
| 4 | `u32` | tile count |
| | | tiles, each the 8-byte tile header above, then its points |

Each point is `s16` x, y, z and a `u16` link, where the link is now the index
of the neighbouring tile in this list, or `0xFFFF` for none. Tiles are kept in
file order, so a room's tiles stay contiguous. Reader:
`src/resource/factory/StanFactory.cpp`.
