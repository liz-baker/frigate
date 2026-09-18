# GoldenEye model node tree

Read by `tools/o2r_packer/ge_model.py`. Multi-byte values are big-endian.
The image is the decompressed model file described in
`goldeneye-containers.md`; every pointer is `0x05000000 + offset`, and `0`
is null.

## Image layout

In order from offset 0, with no header:

1. Switch array: `numSwitches` pointers (entries may be null).
2. Texture table: `numtextures` entries of `0x0C` bytes.
3. The root node, then the rest of the nodes and their data.

`numSwitches` and `numtextures` are not in the file. The game keeps them
in a per-model header table compiled into its code. The GoldenEye decomp
lists them as `MODELFILEHEADER(name, ...)` entries in each model's
`modelFileHeader.inc.c`: the fourth argument is `numSwitches`, the
eighth is `numtextures`. `name` is the file name without its one-letter
directory prefix and `Z` suffix (`CcamguardZ.bin` is `camguard`). All 512
chr/gun/prop files have exactly one entry. Head files have
`numSwitches = 0`, so no switch entry can be used to find the root.

## Texture table entry

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | texture ID |
| `0x04` | `u8` | width |
| `0x05` | `u8` | height |
| `0x06` | `u8` | mip-map tile count |
| `0x07` | `u8` | type |
| `0x08` | `u8` | render depth (pixel size code) |
| `0x09` | `u8` | S wrap flags |
| `0x0A` | `u8` | T wrap flags |
| `0x0B` | | padding |

## Node record

`0x18` bytes:

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u16` | opcode; the low byte is the node type |
| `0x02` | | padding |
| `0x04` | pointer | data record |
| `0x08` | pointer | parent |
| `0x0C` | pointer | next sibling |
| `0x10` | pointer | previous sibling |
| `0x14` | pointer | first child |

The tree is walked from the root by child and next links. Observed
across all 512 files: a node's child has it as parent, a node's next has
the same parent and has it as previous, and the root has no parent.

Two node types name their child in the data record as well. The game
copies that pointer into the child link at load, so the file's own child
link can be null:

| Type | Field holding the child |
|---|---|
| `0x08` LOD | data `0x08` |
| `0x12` switch | data `0x00` |

A `0x09` BSP node's left and right pointers (data `0x18`, `0x1C`) name its
child and that child's next sibling, so they add no edges.

## Display-list node data records

Offsets into the data record:

| Type | Primary list | Secondary list | Vertex pointer | Vertex count |
|---|---|---|---|---|
| `0x04` plain | `0x00` | `0x04` | `0x0C` | `0x10` (`u16`) |
| `0x18` collision | `0x00` | `0x04` | `0x08` | `0x0C` (`s16`) |
| `0x16` primary-only | `0x08` | none | `0x04` | `0x00` (`s32`) |

Vertices are 16 bytes each. For `0x04` and `0x18` the count is the number
of vertices the lists load. For `0x16` the lists load four times the
count (all five such nodes in the US ROM's model files agree). One `0x04`
node, in `PexplosionbitZ.bin`, has count 0 and still loads 16 vertices;
its count is not a usable bound.

## Texture references in display lists

Across all 512 files, model display lists reference textures two ways:

- `G_SETTEX` (`0xC0`, 5,846 uses): `w1` is a texture ID present in the
  file's own texture table. This is how models select their textures.
- `G_SETTIMG` (`0xFD`, 30 uses, in the goldeneye-logo, legal-page and
  nintendo-logo props): `w1` is a segment-5 address inside the
  image, so these props carry their pixel data in the model file itself
  rather than in the texture table.
