# Character model table

Written by `tools/o2r_packer/pack_chr_records.py` into the objects archive at
`chr/records` (`OBLB`, ASCII text). It is the decomp's `c_item_entries`: bodies
and heads share one slot space, heads starting at `HEAD_START`.

One line per slot, tab-separated, in slot order:

| Field | Meaning |
|---|---|
| index | The slot number, from 0 |
| identifiers | Comma-separated `BODY_*` and `HEAD_*` names of the slot. A head slot has both (`BODY_Male_Karl,HEAD_Male_Karl`); a few differ (`BODY_Male_Pierce_Bond_1,HEAD_Male_Brosnan_Boiler`) |
| file | The model's file name; its resources are under `models/chr/<file>` |
| isMale | 1 if a random head for this body is drawn from the male heads |
| hasHead | 1 if the model carries its own head. 0 means a body that takes a separate head model; heads themselves are 1 |
| scale | The scale an instance starts with (`ChrModelFileRecord.scale`), before the game's 0.10000001 |
| pov | The instance's animation translation scale (`ChrModelFileRecord.pov`) |
| radius | The model header's `BoundingVolumeRadius`, in model units |

The identifiers come from the `BODIES` and `HEADS` enums with the `ALL_BONDS`
slots left out. The packer refuses to write if the enums and the record files
disagree on the slot count.

## Hat placements

`chr/hats` (`OBLB`, ASCII text) is the decomp's `headHat_array_8003E464`
(`assets/obseg/chr/chrHeadHats.inc.c`): where a hat sits on a male head. One
line per entry, tab-separated: the entry's index, then `xoffset`, `yoffset`,
`zoffset`, `xsize`, `ysize` and `zsize`. Entries run six to a head (one per
`HATTYPE`, `HATTYPE_BERRET` first), in head order from `HEAD_START`.
