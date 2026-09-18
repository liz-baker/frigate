# Vertex data

GoldenEye's vertices are plain N64 `Vtx` records: 16 bytes, big-endian,
loaded by `G_VTX` and used unmodified by the game (the bg room loader only
decompresses the blob).

| offset | size | field                                      |
|--------|------|--------------------------------------------|
| 0      | 6    | position x, y, z (s16 each)                |
| 6      | 2    | flag (u16)                                 |
| 8      | 4    | texture coordinate s, t (s16 each)         |
| 12     | 4    | colour r, g, b, a (u8), or normal x, y, z (s8) and alpha, depending on lighting mode |

## Where the arrays are

- **Models:** each display-list node's data record points at its own array
  (see `goldeneye-model-nodes.md`); its lists reach it as segment 4. The
  array size is the record's count times 16 bytes (`0x16` nodes: four times
  the count). One `0x04` node, in `PexplosionbitZ`, stores a count of 0 yet
  loads 16 vertices; its size is taken from the furthest byte its lists load.
- **bg rooms:** the room's vertex blob (segment `0x0e`) has no count. Only
  the ranges that the primary and secondary lists load are extracted, merged
  where they overlap or touch.

## `Fast::Vertex` resource

Type `OVTX`, version 0, transcribed from
`external/libultraship-wiiu/src/fast/resource/factory/VertexFactory.cpp`.
Payload after the 20-byte header (see `o2r-resource-header.md`):

| offset | size | field                                             |
|--------|------|---------------------------------------------------|
| 0      | 4    | vertex count (u32)                                |
| 4      | 16 each | `Vtx`: ob[3] (s16), flag (u16), tc[2] (s16), cn[4] (u8) |

The reader takes the header's byte order; the packer writes big-endian, so
the vertex bytes are the N64 bytes unchanged.

## Identity

`ge_vertex.py` keys each block by where it came from: a model block by its
node's offset in the decompressed image, a bg block by room index plus the
range's byte offset in the room's vertex blob. Both are stable for a given
ROM, so a later step can derive resource paths from them.
