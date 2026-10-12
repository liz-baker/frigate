# GoldenEye texture mip chains (`<name>.mip`)

A texture the game mipmaps holds a chain of lower levels once loaded. The
packer writes that chain as a `Fast::Texture` resource at
`textures/<name>.mip`, next to the base image at `textures/<name>`
(`tools/o2r_packer/ge_mipmap.py`, `pack_textures.py`).

## Resource

Same resource layout as any texture (`o2r-resource-header.md`):

| Field | Value |
|---|---|
| texture type | the base image's |
| width, height | the base level's |
| flags | 0 |
| data | every level, base first, back to back |

A paletted chain shares the base image's `<name>.pal`.

The display list factory leaves a chain unused (the request loads its base
level alone) when the `gMipChains` CVar is 0, the graphics menu's
"Mip-Mapped Textures" setting; it is on unless set, and read each time a list
is expanded.

## Levels

- **Count.** The low six bits of the texture's first header byte
  (`goldeneye-texture-bitstream.md`) give how many levels the game holds.
  Fewer than two means no chain, and no `.mip` is written.
- **Paletted limit.** For a paletted image, generation stops at the first
  level that would take the chain past 0x800 bytes (base level included).
  If that leaves only the base level, no `.mip` is written.
- **Sizes.** Each level halves the previous one's width and height, rounding
  up: 5 → 3 → 2 → 1.
- **Rows.** Every row is padded to a whole 64-bit TMEM word: a multiple of 4
  texels for 16- and 32-bit formats, 8 for 8-bit and 16 for 4-bit. A level
  takes `padded row bytes × height` bytes, so its offset in the data is also
  its TMEM word address × 8, which is where the game's tile for the level
  points.
- **Byte order.** Rows are stored in plain order. The game swaps the 32-bit
  halves of every odd row in memory, to suit a TMEM load with no dxt; the
  packer does not.

## Generation

Each level is generated from the one before it, the way the game does at
load time:

| Format | Rule |
|---|---|
| RGBA16, IA16, IA8, I8, RGBA32 | Each output texel averages a 2×2 block, channel by channel. Colour channels round down. Alpha rounds to nearest. On an odd width or height, the last column or row is read twice. |
| I4, IA4 | Each output byte comes from two source bytes and the two below them. The high nibble is built from the left pair, the low nibble from the right pair. |
| CI4, CI8 | Average the four palette colours, then map the average to the closest palette entry. RGBA palettes compare in RGBA5551 with an alpha mismatch weighted 31². IA16 palettes compare by (intensity, alpha). |

The closest-entry search is approximate in the game. It takes an exact match
if there is one. Otherwise it binary-searches the palette by squared
magnitude and checks only the four entries either side of where that search
stops.

The IA16 CI4 path reads entry 0 or entry 4 of the palette for the
right-hand texel of each pair: 0 when its source byte, shifted by one bit
when that texel is inside the image, is nonzero, and 4 otherwise. The other
paths read their own texels.

The game reads colours from a 256-entry table that holds only the image's own
entries. Entry 4 of a palette with fewer than five entries is therefore
whatever an earlier load left in that table. The packer can't know that, so
it reads such entries as 0.

Not covered yet:
- **Explicit levels.** A texture with the "has explicit LODs" bit carries its
  own lower levels in the bitstream; the packer generates them like any other
  (https://github.com/liz-baker/frigate/issues/458).
- **32-bit images.** These get no chain
  (https://github.com/liz-baker/frigate/issues/459).
