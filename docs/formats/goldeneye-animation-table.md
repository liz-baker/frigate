# Animation tables

Written by `tools/o2r_packer/pack_animation_table.py` into the objects archive
at `animations/table` (`OBLB`, ASCII text). It is the decomp's two animation
tables in `initanitable.c`: the characters' `animation_table_ptrs1`, whose index
is the animation id the AI and the character code start animations by, and the
aircraft's `animation_table_ptrs2`.

One line per entry, tab-separated, characters' table first and each in index
order:

| Field | Meaning |
|---|---|
| table | `1` for the characters' table, `2` for the aircraft's |
| index | The entry's index in its table, from 0 |
| name | The clip's name: the clip is the `animations/<name>` resource (`goldeneye-animation-clip-resource.md`). `-` for an id with no clip (the decomp's `null<N>` placeholders) |

The packer refuses to write if a name has no clip.
