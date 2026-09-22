# Model node tree (`GEMD`)

Written by `tools/o2r_packer/ge_modeltree.py` to `<model>/tree` (paths:
`goldeneye-display-list-resource.md`), read by
`src/resource/factory/ModelTreeFactory.cpp`. Input is the parsed tree
described in `goldeneye-model-nodes.md`. Version 1, big-endian, after the
header in `o2r-resource-header.md`.

## Payload

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | node count |
| `0x04` | `u32` | matrix count |
| `0x08` | | node records, `0x2C` bytes each, in preorder |
| — | | LOD table (below), immediately after the node records |

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

## LOD table

A `0x2C`-byte fixed record on every node wastes space carrying fields only
a `0x08` (LOD) node uses, so those live in a separate trailing table
instead, one entry per LOD node in the tree:

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | LOD entry count |
| `0x04` | | entries, `0x0C` bytes each |

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | node record index (into the node records above) |
| `0x04` | `f32` | min distance |
| `0x08` | `f32` | max distance |

Transcribed from the GoldenEye decomp's `ModelRoData_LODRecord`: min/max
distance gate whether the LOD node's child (its data record's `Affects`
pointer, already the node's resolved child link) is drawn.

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

## Head attachment

A `kHeadPlaceholderNodeType` (`0x17`) node marks where a body model's head
file grafts on; which head, if any, is a per-character choice the model
file doesn't carry, so the caller passes a `HeadAttachment` (placeholder
offset, node array, matrix count) into `WalkModel`. A head file has no
matrix-owning nodes of its own, so its lists all name matrix 0 literally;
`WalkModel` reproduces that by walking the head's tree with the
placeholder's own inherited matrix as `base` (a placeholder owns no matrix
itself, so this is its nearest owning ancestor's), which lands in the head
walk's matrix slot 0 since nothing in a headless head tree overwrites it.
The result is appended to `ModelWalk::attachments` rather than folded into
the body's own `matrices`/`lists`, since a head's "matrix 0" is not the
same array slot as the body's.

## Switch gating

A `kSwitchNodeType` (`0x12`) node's data record names one child (the
tree's own child link, already resolved at pack time - see "Two node
types name their child" above) and gates it on or off; the game sets that
on/off state from model-specific gameplay logic (e.g. a character's
held-item state), not from anything in the model file, so `WalkModel`
takes a caller-supplied `visibleSwitches` list of switch node offsets to
treat as on. A switch not listed is off, matching its rwdata's own
zero-initialized default before any such gameplay logic runs; nothing
under an off switch is visited (no lists, no descendants).

## What is not supplied

- Animation: no per-joint rotation is applied; every model is at rest
  pose. Tracked by #65.
- BSP front/back ordering: needs the raw plane point and vector that
  `ge_modeltree.py` does not currently carry into the packed node record,
  so BSP children keep file order. Tracked by #64.
