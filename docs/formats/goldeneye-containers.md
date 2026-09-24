# GoldenEye model and bg containers

Read by `tools/o2r_packer/ge_container.py`. Multi-byte values are
big-endian.

## Compression

Every compressed blob below starts with the two-byte marker `11 72`
followed by a raw DEFLATE stream (no zlib wrapper). This is the same
container `ge_texture.py` reads; the marker `11 73` with a five-byte
header also exists there but has not been seen in a model or bg blob. A
stream ends by itself, so data after it (the next blob in a bg file) is
ignored.

## Model files (`assets/obseg/{chr,gun,prop}/*Z.bin`)

The whole file is one blob. Decompressed, it is an image meant to be
loaded at segment 5 (`0x05000000`): every pointer in it is
`0x05000000 + offset`. The file has no header of its own:

- The image starts with the switch pointer array, then the texture
  table, then the node records. The two counts that size the first two
  live outside the file; how to get them, the texture table and the node
  layout are in `goldeneye-model-nodes.md`.

Node records and their display-list layouts: `goldeneye-model-nodes.md`.

Segments used inside a node's display lists:

| Segment | Meaning |
|---|---|
| `5` | this file's image |
| `4` | the node's own vertex array; the game points it there before drawing, so `G_VTX` addresses are relative to the node's vertex pointer |
| `3` | matrices built at run time; no bytes in the file |

## bg files (`assets/obseg/bg/bg_*_all_p.bin`)

The file itself is uncompressed; each room's blobs are compressed
individually. Addresses in the header and room table are segment `0x0F`
addresses (`0x0F000000 + file offset`).

- Header words: `0` is 0 in every file with rooms, `1` the address of the
  room table, `2` the portal table, `3` the visibility data (`0` when
  absent), `4` an array of `f32` the visibility data indexes (`0` when
  absent). The room table starts at file offset `0x14`, right after them.
- The room table has `0x18`-byte entries: vertex-blob address,
  primary-list address, secondary-list address (each `0` when absent),
  then three `f32` room position values.
- Entry 0 is a placeholder. Real rooms start at entry 1 and the table
  ends at the first entry whose primary-list address is `0`.
- The last non-null entry is not a room: it holds end pointers that give
  the final room's blobs their compressed size. A room's blobs are read
  from their address; only the game itself needs the sizes.
- A room can have no vertex blob (a null vertex address with a primary
  list that is a few commands long).

### Portals

The portal table is a run of 8-byte entries ended by one whose first word
is `0`: portal-points address (`u32`), then four bytes: the two room
indices the portal joins, control byte 1, control byte 2. At the
portal-points address: a point count (`u8`), three padding bytes, then
that many points of three `f32`. A portal has at least three points; the
first three span its plane.

### Visibility data

Word 3 points at 8-byte records: a type byte, three padding bytes, and a
`u32` argument, ended by a record of type `0`. When a record's type is
`100`, its argument is the address of a portal's points; the game
replaces it with that portal's index in the table. The bytes are also
read as variable-length commands during visibility traversal.

### Header region

The game reads the start of the file, up to the first room's vertex blob
rounded up to 16, before it touches any room. Everything above lives in
that span, and all its addresses fall inside it. The packer stores it as
`bg/<name>/header` (a blob, `bg/bg_dam/header` for `bg_dam_all_p.bin`).

Inside a room's display lists, `G_VTX` addresses are segment `0x0E`
addresses: `0x0E000000 +` an offset into that room's decompressed vertex
blob.
