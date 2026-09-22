# Model node tree (`GEMD`)

Written by `tools/o2r_packer/ge_modeltree.py` to `<model>/tree` (paths:
`goldeneye-display-list-resource.md`), read by
`src/resource/factory/ModelTreeFactory.cpp`. Input is the parsed tree
described in `goldeneye-model-nodes.md`. Version 0, big-endian, after the
header in `o2r-resource-header.md`.

## Payload

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | node count |
| `0x04` | `u32` | matrix count |
| `0x08` | | node records, `0x2C` bytes each, in preorder |

Preorder is child before next sibling, so the root is record 0. A LOD or
switch node's child is its data-record target when its own child link is null.

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | node offset in the model file (keys its list resources) |
| `0x04` | `u8` | node type |
| `0x05` | | padding |
| `0x06` | `s16` | matrix index, `-1` for none |
| `0x08` | `s16` | bend matrix index, `-1` for none |
| `0x0A` | | padding |
| `0x0C` | `s32` | first child (record index), `-1` for none |
| `0x10` | `s32` | next sibling, `-1` for none |
| `0x14` | `s32` | parent, `-1` for none |
| `0x18` | `f32` x3 | origin |
| `0x24` | `u32` | primary list offset, `0xFFFFFFFF` for none |
| `0x28` | `u32` | secondary list offset, `0xFFFFFFFF` for none |

The list offsets complete the resource path `<model>/dl/<node>/<list>`.
The matrix count is one past the highest matrix index any node names.

## Matrices

A model's display lists load their transform with `G_MTX` from segment 3.
Every such address is a whole multiple of `0x40` (all 3,502 uses in the US
ROM's chr, gun and prop files), so it is an index into an array of
`0x40`-byte fixed-point matrices that the game builds for the model at run time
and points segment 3 at before drawing. Nodes that own an index:

| Type | Index at (data record) | Origin at |
|---|---|---|
| `0x01` header | `0x02` | none |
| `0x02` group, `0x03` | `0x0E` (bend index: `0x10`) | `0x00` |
| `0x15` simple group | `0x0C` | `0x00` |

A node's matrix is a translation by its origin, then its nearest
matrix-owning ancestor's matrix (the object's base matrix above the root).
The game also rotates a joint by its animation state before the translation;
a model at rest pose has none, which is all the walk supplies. The bend
matrix of a group equals its ordinary one at zero rotation.

The matrices the game builds already include the camera, since the list's
`G_MTX` loads (replaces) the modelview matrix; the walk is given the camera
modelview as its base.

### Decision: the packer marks the address

The interpreter resolves a segment address only if its low bit is set, and
GoldenEye's addresses never have it. The packer sets that bit on every
segment-3 `G_MTX` and rejects an address that is not a whole matrix offset.
The walk then needs no per-command rewriting: it fills the array and
points segment 3 at it.

## What is not supplied

- Head models have no matrix nodes; their lists name matrix 0, which the
  game takes from the body node a head attaches to. Standalone, the walk leaves
  it as the base matrix.
- Animation, LOD selection by distance, switch state and BSP ordering:
  every LOD and switch child is drawn and BSP children keep file order.
