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

## File select entries

The six entries of `s_mainfolderimages` at `0xFD8` to `0x1020`, in
`MAINFOLDERIMGIDS` order, are the file select screen's: the copy icon, the erase
icon, the "select file" title, the cross, the tick and the dot. They are plain
images packed like the HUD's.

## Effect entries

The same table holds the images the in-level effects select: the flare, the
twenty bullet impact images, the six smoke and five debris frames, and the
five flares at `0xAC8` to `0xC84`, the glass overlay at `0xD44` to `0xD5C`, and
the fifty-one images the monitors' screens show (`s_monitorimages`, `0xD5C` to
`0xFC0`, indexed by the number a screen's image command names). They are
packed the same way, with the half-texel sample offset the effects select them
with, and unlike the HUD's they may be mipmapped; a texture
resource holds only the base level, so they load as one tile
(`goldeneye-display-list-resource.md`, "Textures").

## Radar entry

The multiplayer radar's circle (`s_mpradarimages`, `0x1020`) is a 32 by 32
mipmapped image. It is packed with the effects' (`hud/fx/1020`, half-texel sample
offset, base level only), which the radar selects with `SelectEffectImage`.

## Character select entries

The sixty-four entries of `s_mpcharselimages` at `0x102C` to `0x132C` are the
multiplayer character select's portraits. Each of the sixteen portraits
(`MPCHRSELIMAGES` order) is four 65 by 65 four-bit intensity images: the upper
left, upper right, lower left and lower right quarters, 65 by 67 for the "who"
portrait of the characters without one. They are packed like the effect entries:
mipmapped in the ROM, base level only here, selected with the half-texel sample
offset.

## Display lists

The table starts with the display lists the explosions and the debris select
their images with (`0x000` to `0xAC8`). All are packed: the smoke a smoke
cloud is drawn with (`0x000`), the static noise the debris is drawn with
(`0xA50`), and the fifteen frames of an explosion's flare (`0x078` to `0x9A8`,
`0xA8` apart), which blend a smoke and a fire image. Each is the list as the ROM
has it, with the commands that load an image (the texture, the tile that loads
it, the load and the tile size) replaced by the `G_SETTEX` request and the
texture reference. A flare's first image is requested like a single image; its
second takes the two-image request (type 5), which carries the TMEM address and
wrap modes of the render tile the ROM sets for it.

## Resources

| Resource | Contents |
|---|---|
| `hud/img/<offset>` (`GEDL`) | The list for the HUD's entry at `<offset>`, four hex digits: the `G_SETTEX` request (single tile, no sample offset, wrapping as the entry's flags say), the reference to `textures/<name>` and the end of the list. |
| `hud/fx/<offset>` (`GEDL`) | The same for an effect's entry, with the half-texel sample offset. |
| `hud/gdl/<offset>` (`GEDL`) | A display list of the table: `0x000`, `0xA50` or one of the flare's fifteen. |
| `hud/images` (`OBLB`, text) | One line per entry, tab-separated: `0x<offset>`, width, height, format (the N64 image format code). The HUD lays its images out by these sizes, and a monitor picks its combiner by the format. |
