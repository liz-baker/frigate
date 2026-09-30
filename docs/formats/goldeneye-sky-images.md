# Sky and water images

Written by `tools/o2r_packer/pack_sky_images.py` into the geometry archive. The
sky draws its cloud plane and its sea from the three entries of the game's
`skywaterimages` table, part of the global image table described in
`goldeneye-hud-images.md` (12-byte entries, texture id, size, mip levels,
format, wrap flags). The table sits at offset `0xFB4` of it.

| Entry | Image | Used as |
|---|---|---|
| 0 | `CLOUDS_GRAYSCALE`, 64x64 IA 8-bit | the cloud plane (`SkyImageId`) |
| 1 | `WATER_GRAYSCALE`, 64x64 I 8-bit | a water plane (`WaterImageId`) |
| 2 | `WATER_BLUE`, 32x32, six mip levels | a water plane (`WaterImageId`) |

## Resources

| Resource | Contents |
|---|---|
| `sky/img/<index>` (`GEDL`) | The list the game's `texSelect()` builds for the entry: the `G_SETTEX` request, the reference to `textures/<name>` and the end of the list. Like a gunfire or shadow image (`goldeneye-display-list-resource.md`, "Effect images"), the request is a single tile with a half-texel sample offset, wrapping as the entry's flags say, and a mipmap request when the entry has levels. |

Only the base level of a mipmapped image exists.

The water's image is stored as 8-bit palette indices with its palette, where
the game keeps it expanded to 16-bit colour, so the water's tile setup
(`Sky::SetupWater`) declares its tiles as paletted 8-bit.
