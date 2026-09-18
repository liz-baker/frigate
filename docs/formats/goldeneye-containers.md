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
  table, then the node records. The counts that locate the texture table
  and the root node are held in game code, not in the file, so a
  reader starts from a node pointer in the switch array and climbs
  `Parent` links to reach the root.
- A node is `0x18` bytes: `u16 opcode` (low byte is the node type), pad,
  then `data`, `parent`, `next`, `prev`, `child` pointers.

Display-list node types (`data` layouts, offsets in bytes):

| Type | Primary list | Secondary list | Vertex pointer | Vertex count |
|---|---|---|---|---|
| `0x04` plain | `0x00` | `0x04` | `0x0C` | `0x10` (`u16`) |
| `0x18` collision | `0x00` | `0x04` | `0x08` | `0x0C` (`s16`) |
| `0x16` primary-only | `0x08` | none | `0x04` | `0x00` (`s32`) |

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

- Header word 1 is the address of the room table.
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

Inside a room's display lists, `G_VTX` addresses are segment `0x0E`
addresses: `0x0E000000 +` an offset into that room's decompressed vertex
blob.
