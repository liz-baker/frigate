# Model node tree (`GEMD`)

Written by `tools/o2r_packer/ge_modeltree.py` to `<model>/tree` (paths:
`goldeneye-display-list-resource.md`), read by
`src/resource/factory/ModelTreeFactory.cpp`. Input is the parsed tree
described in `goldeneye-model-nodes.md`. Version 3, big-endian, after the
header in `o2r-resource-header.md`.

## Payload

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | node count |
| `0x04` | `u32` | matrix count |
| `0x08` | | node records, `0x2C` bytes each, in preorder |
| — | | LOD table (below), immediately after the node records |
| — | | BSP table (below), immediately after the LOD table |
| — | | joint ID table (below), immediately after the BSP table |

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

## BSP table

The same reasoning as the LOD table applies to `0x09` (BSP) node fields,
in a table immediately after the LOD table, one entry per BSP node:

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | BSP entry count |
| `0x04` | | entries, `0x24` bytes each |

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | node record index (into the node records above) |
| `0x04` | `f32` x3 | plane point |
| `0x10` | `f32` x3 | plane vector |
| `0x1C` | `s32` | vector transform mode |
| `0x20` | `s32` | second child group's head (record index), `-1` for none |

Transcribed from the GoldenEye decomp's `ModelRoData_BSPRecord`. The
node record's own first-child link is already the first child group's
head (the tree's own child/next links resolve both groups' siblings
into one chain, first group before second); the second group's head is
not otherwise derivable, so it is carried here. Vector transform mode
selects how the plane vector is transformed by the node's own matrix
before the front/back test: `0` rotates it by the matrix's full 3x3,
`1`/`2`/`3` scale it by the matrix's row 0/1/2.

## Joint ID table

The same reasoning as the LOD and BSP tables applies to the `JointID`
field a group node's (`0x02`/`0x03`) `ModelRoData_GroupRecord` carries:
a table immediately after the BSP table, one entry per group node.

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | joint entry count |
| `0x04` | | entries, `0x06` bytes each |

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | node record index (into the node records above) |
| `0x04` | `u16` | joint ID |

Transcribed from the GoldenEye decomp's `ModelRoData_GroupRecord`.
`JointID` indexes the model's own joint table, packed separately as a
`GESK` resource: `goldeneye-model-skeleton-resource.md`. A group node
without an entry here has joint ID `0`.

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
a group node (`0x02`/`0x03`) with a caller-supplied `AnimationPose` (a clip,
the model's joint table, a fractional frame and a mirror flag) is rotated by
its `JointID`'s sampled channel first -- see "Animation" below. Any other
matrix-owning node, or a group node with no pose or no joint-table entry,
stays at rest pose. The bend matrix of a group equals its ordinary one
(neither the bend-specific single-axis rotation nor animation blending is
applied to it -- #72).

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

## BSP reorder

A `0x09` (BSP) node's own inherited matrix (`own`, since it owns no
matrix itself) transforms the BSP table entry's plane point (the full
affine transform) and vector (per the vector transform mode above); the
dot product of the two decides which child group is visited first: a
negative dot product visits the first group (the node's own child link)
before the second (the table entry's second-group head), a
non-negative one visits the second group first. Both groups are always
visited -- this only reorders them, matching the decomp's "Z buffer
off" compositing order, not a switch-style gate.

## Animation

A group node's `JointID` indexes the `AnimationPose`'s joint table
(`goldeneye-model-skeleton-resource.md`'s `SkeletonJoint`, though `WalkModel`
takes a resource-independent `JointChannels` to keep the model layer free of
a resource-layer dependency) to a `mtxA`/`mtxB` channel pair; the pose's
`mirror` flag (the decomp's single per-model `flip`, e.g. reusing a gun-hand
animation on the other hand) picks `mtxB` over `mtxA`. `Animation::
SampleJointRotation` decodes and interpolates that channel from the pose's
clip and fractional frame (`goldeneye-animation-pose.md`), and the result
rotates the node before its origin translation, composed with the parent the
same way the identity-rotation case already was. A pose is not passed into
a head attachment's own recursive walk -- a head has no matrix-owning nodes
of its own to animate.

## What is not supplied

- Root motion and multi-animation blending (quaternion slerp between a
  primary and secondary clip, and the bend node's own single-axis rotation
  path): #72, explicitly deferred.
