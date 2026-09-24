# Model node tree (`GEMD`)

Written by `tools/o2r_packer/ge_modeltree.py` to `<model>/tree` (paths:
`goldeneye-display-list-resource.md`), read by
`src/resource/factory/ModelTreeFactory.cpp`. Input is the parsed tree
described in `goldeneye-model-nodes.md`. Version 6, big-endian, after the
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
| — | | switch table (below), immediately after the joint ID table |
| — | | sort table (below), immediately after the switch table |
| — | | effect table (below), immediately after the sort table |

Preorder is child before next sibling, so the root is record 0. A LOD or
switch node's child is its data-record target when its own child link is null.

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | node offset in the model file (keys its list resources) |
| `0x04` | `u8` | node type |
| `0x05` | `u8` | render type (below), `0` for a node with none |
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

The render type is a display-list node's (`0x04`, `0x18`) `ModelType`: which
pre-defined render-mode and combiner setup the game applies before drawing
its lists (`0` none, `1` one cycle, `2` two cycle, `3` gun lighting, `4`
fog/lighting object). The packer rejects any other value.
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
pointer, already the node's resolved child link) is drawn. The child shows if
the node's distance is above the minimum (or the minimum is 0) and no more than
the maximum, both times the model's scale. The distance is the node's depth in
view space times the player's LOD scale and the global distance scale, or 0
when the game has distance disabled (`modelSetDistanceDisabled`, as the intro's
character does), where only a minimum of 0 shows: `LodSettings` in
`model/ModelWalk.h`.

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
field a group node's (`0x02`/`0x03`) `ModelRoData_GroupRecord` carries, and
to the `AnimPart` field of a header node's (`0x01`) `ModelRoData_HeaderRecord`
(the joint whose root-motion channels move the model as a whole): a table
immediately after the BSP table, one entry per group or header node.

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
`GESK` resource: `goldeneye-model-skeleton-resource.md`. A node without an
entry here has joint ID `0`.

## Switch table

The model file starts with an array of node pointers (the decomp's
`ModelFileHeader.Switches`, `goldeneye-model-nodes.md`); game code refers to
nodes by position in it, e.g. the node a held gun attaches to. A table
immediately after the joint ID table, one entry per array slot, in order:

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | switch entry count |
| `0x04` | | entries, `0x04` bytes each |

| Offset | Type | Field |
|---|---|---|
| `0x00` | `s32` | node record index (into the node records above), `-1` for a null slot |

## Sort table

The game draws a character joint by joint, back to front, since some scenes
run with the z-buffer off. The joints are the subtrees rooted at header
(`0x01`), group (`0x02`), simple group (`0x15`), gunfire (`0x0c`), shadow
(`0x0d`) and interlink (`0x0f`) nodes; each is ordered by a depth taken from
its matrix and the data below. A table immediately after the switch table,
one entry per such node that has any:

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | sort entry count |
| `0x04` | | entries, `0x20` bytes each |

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | node record index (into the node records above) |
| `0x04` | `s32` | other node (record index), `-1` for none |
| `0x08` | `f32` x3 | point |
| `0x14` | `f32` x3 | second point |

The other node is a header's first group (`ModelRoData_HeaderRecord.
FirstGroup`), a group's child group (`ModelRoData_GroupRecord.ChildGroup`),
or a shadow's header node. The point is a gunfire's offset, a shadow's
`(pos.x, 0, pos.y)`, or an interlink's `pos`; the second point is an
interlink's `pos2`. Nodes of types `0x03`, `0x0b`, `0x0e` and `0x10` also
start a draw entry in the game, but no US ROM model contains one, so the
packer rejects them rather than carry data nothing reads.

## Effect table

The quads a gunfire (`0x0c`) or shadow (`0x0d`) node draws are built by the
game each frame, so what the tree carries is their size and image. A table
immediately after the sort table, one entry per such node:

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | effect entry count |
| `0x04` | | entries, `0x18` bytes each |

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | node record index (into the node records above) |
| `0x04` | `f32` x3 | size: a gunfire's `Size`, or a shadow's size in x and y with z `0` |
| `0x10` | `u8` | `1` if the quad is textured, else `0` |
| `0x11` | `u8` | image width |
| `0x12` | `u8` | image height |
| `0x13` | `u8` | mip level count (`0` for a single level) |
| `0x14` | `u8` | the image's N64 format code (`0` RGBA, `2` CI, `3` IA, `4` I) |
| `0x15` | | padding |

Transcribed from the GoldenEye decomp's `ModelRoData_GunfireRecord` and
`ModelRoData_ShadowRecord`; the texture fields come from the texture table
entry their `Image` pointer names (`goldeneye-model-nodes.md`). A textured
node's image is selected by a list at `<model>/fx/<node offset>`
(`goldeneye-display-list-resource.md`). The gunfire's offset and the shadow's
position are in the sort table, since that is where the game takes their depth.

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
the model's joint table, two frames with a fraction between them, and a
mirror flag) is rotated by
its `JointID`'s sampled channel first -- see "Animation" below. Any other
matrix-owning node, or a group node with no pose or no joint-table entry,
stays at rest pose. A group node's bend matrix is rotated by *half* the
same sampled rotation instead -- see "Bend matrix" below.

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
takes a caller-supplied `hiddenSwitches` list of switch node offsets to
treat as off (a head attachment carries its own list for the head's
switches). A switch not listed is on, as the decomp's `modelInitRwData`
leaves every switch; nothing under an off switch is visited (no lists, no
descendants).

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
clip and frames (`goldeneye-animation-pose.md`), and the result
rotates the node before its origin translation, composed with the parent the
same way the identity-rotation case already was. A pose is not passed into
a head attachment's own recursive walk -- a head has no matrix-owning nodes
of its own to animate.

## Bend matrix

A group node's opcode flag `0x100` (`MODELGROUP_MTX_HAS_MATRIX1`) means it
writes a second matrix (its bend matrix, `ModelNode::bendMatrix`) at *half*
its primary joint rotation, for elbow/shoulder-style bend correctness --
verified against the real US ROM's chr/gun/prop group nodes (1145 total):
`bendMatrix != kNoMatrix` correlates exactly with the flag being set in
every one of them (195, ~17%, e.g. `CarmourguardZ`), so no separate flag
needs carrying -- `bendMatrix`'s presence already says everything this
needs. The half rotation is not a plain Euler half-angle: it's the
sampled rotation converted to a quaternion, then slerped from the
identity quaternion by `t=0.5` (`Quaternion::SlerpFromIdentity`,
transcribed from the GoldenEye decomp's `quaternion_7F05BC68`), and that
quaternion becomes the node's local rotation the same way the ordinary
matrix's Euler rotation does. At rest pose (no sampled rotation) this
degenerates to the identity quaternion, so the bend matrix matches the
ordinary one exactly, same as before.

A second opcode flag, `0x200` (`MODELGROUP_MTX_HAS_MATRIX2`), names a
further elbow/knee-style bend+stretch-scale variant (`ModelRoData_
GroupRecord.MatrixID2`) -- confirmed dead in retail data (0 of the same
1145 group nodes set it), matching the GoldenEye decomp's own struct
comment marking that field "never used". Not implemented; not worth it.

## Secondary-animation blend

`AnimationPose` optionally carries a second clip (`secondaryClip`, with its
own two frames, fraction and `secondaryMirror` flag), for transitions between
poses -- the same joint table as the primary clip applies. When set, a group node's
rotation is no longer the primary clip's Euler angles directly: both
clips' sampled rotations become quaternions (`Quaternion::
EulerToQuaternion`), the second is flipped toward the shorter path if
needed (`Quaternion::EnsureShortestPath`), and the two are slerped by
`blend` (`Quaternion::Slerp`, the general two-quaternion form) --
transcribed from `process_02_position`'s `model->unk84 != 0.0f` branch.
The result becomes the node's local rotation (`QuaternionRotatedAndTranslated`)
in place of the Euler path, for both the ordinary matrix and, when present,
the bend matrix (which slerps the *blended* quaternion from identity by
`t=0.5`, same as the primary-only case). `blend=0` or no `secondaryClip`
is exactly the primary-only rotation.

## Root placement

A header node (`0x01`) owns a matrix like any other, but the game builds it
from the model's root state rather than from the node record: a rotation
about Y by the model's heading, translated to its position, with the 3x3
part scaled by the model's scale (`process_01_group_heading`). The caller
passes that as a `RootPlacement` (`ModelPlayback` produces it, including the
heading override blend); without one the header owns the inherited matrix
unchanged, as a static prop's does.
