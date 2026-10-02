# Texture surface types

Written by `tools/o2r_packer/pack_texture_hits.py` into the geometry archive
as `textures/hits` (`OBLB`, text). A shot that hits a room's or a model's
triangle learns its image from the list's texture reference, and chooses the
mark it leaves and the sound it makes from that image's surface types, which
the game keeps in its image table.

One line per image, in the order of the decomp's `images.def` (the game's
texture number is the line's position, from 0), space separated:

| Field | Meaning |
|---|---|
| name | the image's name; its resource is `textures/<name>` |
| hit sound | the `HIT_*` value the hit sound is chosen by |
| hit texture | the `HIT_*` value the impact mark is chosen by |

`HIT_*` values count from `HIT_DEFAULT` (0) in the order of `enum HIT_TYPE` in
the decomp's `bondconstants.h`.
