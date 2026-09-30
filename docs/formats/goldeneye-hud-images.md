# HUD images

Written by `tools/o2r_packer/pack_hud_images.py` into the geometry archive. The
in-level HUD draws the crosshair and the ammo icons from entries of the game's
global image table, initialised data in the ROM: 12-byte entries, each naming a
texture by its index in `images.def` (the `IMAGE_*` ids), its size, its
format and its wrap flags. The HUD selects an entry with the game's
`texSelect()`, which loads the texture as one tile. Every entry the HUD uses is
written as a display list of its own, the same request an effect image
(`goldeneye-display-list-resource.md`, "Effect images") carries, so the HUD
selects a texture by calling a list.

## Entries

The entries at table offsets `0xC84` to `0xD3B`, in steps of 12: the thirteen
ammo icons (`ammo_related`'s `IconImage`, `0xC84` to `0xD20`) and the two
crosshairs (`0xD2C` is the one the HUD draws, `0xD38` the beta one). Each must
be a plain image without mip levels.

| Offset | Field |
|---|---|
| 0 | texture id, big-endian u32 |
| 4 | width, u8 |
| 5 | height, u8 |
| 6 | mip levels, u8 (0) |
| 7 | format, u8 |
| 8 | pixel size, u8 |
| 9 | wrap flags in S, u8 (`G_TX_*`) |
| 10 | wrap flags in T, u8 |

## Resources

| Resource | Contents |
|---|---|
| `hud/img/<offset>` (`GEDL`) | The list for the entry at `<offset>`, four hex digits: the `G_SETTEX` request (single tile, no sample offset, wrapping as the entry's flags say), the reference to `textures/<name>` and the end of the list. |
| `hud/images` (`OBLB`, text) | One line per entry, tab-separated: `0x<offset>`, width, height. The HUD lays its images out by these sizes. |
