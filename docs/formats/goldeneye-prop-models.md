# Object model table

Written by `tools/o2r_packer/pack_prop_models.py` into the objects archive at
`prop/models` (`OBLB`, ASCII text). It holds one line per `PROP` value, in
`PROP` order: the decomp's `PitemZ_entries`, which a setup file's object
records index by their `model` field.

Tab-separated `model` lines:

| Field | Meaning |
|---|---|
| index | The `PROP` value, from 0 |
| name | The record's model name (`alarm1`), the `PROPFILERECORD(<model>, <scale>)` argument the model's file is named after |
| scale | The model scale (`ItemModelFileRecord.scale`) an instance starts with; `0.1` when the record leaves it empty |
| xmin, xmax, ymin, ymax, zmin, zmax | The model's bounding box in model units, or `-` for each bound when the model has none |

The model is the file `P<model>Z` under `prop/`, where `<model>` is the name
in the record's `PROPFILERECORD(<model>, <scale>)`. Its bounding box is the
first bounding-box node (`0x0A`) that `chrobjGetBboxFromObjFile` finds: among
the root node's children, else among its first child's. The node's data record
is a `u32` model number, then the six bounds as big-endian `f32`.

The packer refuses to write if `PROP` and the table disagree on the count, or
a model is not extracted.
