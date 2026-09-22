# Model skeleton (`GESK`)

Written by `tools/o2r_packer/ge_skeleton.py` to `<model>/skeleton` (paths:
`goldeneye-display-list-resource.md`) for every model whose
`MODELFILEHEADER` names a skeleton (a head file's `NULL` skeleton gets no
`GESK` resource). Version 0, big-endian, after the header in
`o2r-resource-header.md`.

## Source

Unlike everything else this port packs, a skeleton is not reachable from
its model's own bytes -- the GoldenEye decomp's `ModelFileHeader.Skeleton`
is a compiled ROM data-segment pointer with no extractable address short
of building the decomp. What the decomp's own asset extraction does
produce is each skeleton's `ModelJoint JOINTLIST(name)[]` array as plain
C source, under `assets/embedded/skeletons/*.inc.c` -- one file often
holds several skeletons (e.g. every simple prop shares `standard_object`).
`ge_skeleton.parse_skeletons` reads that literal array text the same way
`ge_model.read_model_header_counts` already reads `MODELFILEHEADER`
tables, rather than resolving any pointer.

A model's `MODELFILEHEADER`'s `SKELETON(name)` argument names which
skeleton it uses; several models commonly share one (e.g. every simple
gun prop uses `standard_gun`).

## Payload

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u32` | joint count |
| `0x04` | | joint records, `0x06` bytes each |

| Offset | Type | Field |
|---|---|---|
| `0x00` | `u16` | node type |
| `0x02` | `u16` | `mtxA` (animation channel index, unmirrored) |
| `0x04` | `u16` | `mtxB` (animation channel index, mirrored) |

Transcribed from the GoldenEye decomp's `ModelJoint`. A joint's index in
this array is what a `GEMD` group node's `JointID` (see
`goldeneye-model-tree-resource.md`'s "joint ID table") names; `mtxA`/
`mtxB` are channel indices into an animation clip's per-frame bitstream
(`goldeneye-animation-pose.md`), not model-tree matrix indices despite
the name. Node type is carried for completeness; nothing reads it yet.
