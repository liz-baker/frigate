# Water lists

Written by `tools/o2r_packer/pack_water_lists.py` into the geometry archive at
`bg/water/<image>` (`GEDL`, paths: `goldeneye-display-list-resource.md`), for
the two images the game follows with a call to a list that scrolls them: image
1508 (the Caverns water) and image 1511 (the water in Dam and Complex).

The game's `texLoadFromGdl` emits that call after the texture's own setup while
it loads a room's list (`MipMap2C_Something_Setup` for 1508,
`MipMap2C_Something2_Setup` for 1511); here the packed room lists carry it as a
call by hash after the image's `G_SETTEX` and image reference. Each list
declares tiles 0 and 1 as two views of the one image, sets the primitive
colour, the combiner that blends the tiles by the colour's level-of-detail
fraction, the two-cycle render mode and back-face culling, and ends.

The list is a copy of the game's data, with these commands (0-based) rewritten
every tick by `sub_GAME_7F092E50`, which `Sky::UpdateRoomWater` does here:

| Command | Meaning |
|---|---|
| 2 | `gsDPSetTileSize` of tile 0: upper left corner is the scroll (s, t) |
| 3 | `gsDPSetTileSize` of tile 1: upper left corner is the scroll plus (90, 150), each wrapped to a byte |
| 4 | `gsDPSetPrimColor`: the low byte of the first word is the cross-fade, `sin(phase) * 127 + 128` |

The two lists differ in the tile format only (1508 is a 4 bit intensity image
64 texels square, 1511 an 8 bit palette index image 32 texels square).
