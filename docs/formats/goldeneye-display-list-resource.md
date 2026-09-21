# Re-emitted display lists (`Fast::DisplayList`)

Written by `tools/o2r_packer/ge_dlemit.py`. Input is a list stream as
described in `goldeneye-display-list.md`; output is one `GEDL` (version 0)
resource per list, with every address replaced by the path of another
resource.

## Resource paths

`<model>` is `models/<dir>/<file>` (`assets/obseg/chr/CarmourguardZ.bin` →
`models/chr/CarmourguardZ`); `<room>` is `bg/<bg name>/room<NN>`
(`bg_sev_all_p.bin`, room 3 → `bg/bg_sev/room03`). Offsets are six hex digits.

| Resource | Path |
|---|---|
| Model node's vertices (`ModelVertexKey`) | `<model>/vtx/<node offset>` |
| Model list | `<model>/dl/<node offset>/<list offset>` |
| Model embedded image | `<model>/img/<pixel data offset>` |
| bg vertex block (`BgVertexKey`) | `<room>/vtx/<blob offset of the block>` |
| bg list | `<room>/dl/<primary or secondary>/<list offset>` |
| Texture / palette (from `pack_textures.py`) | `textures/<image name>` / `textures/<image name>.pal` |

A model list is keyed by node as well as offset because the same list
addresses a different vertex array under each node that draws it. Roots are
the node's primary and secondary lists; every list they call is emitted as
its own resource.

## Payload

After the header (`o2r-resource-header.md`): the ucode byte (6,
`ucode_indy_ge`), seven zero bytes so commands start 8-byte aligned in the
file (the reader skips to the next 8-byte boundary after the ucode byte,
and the payload starts at byte 64), then the list's commands exactly as in the source, ending with its
`G_ENDDL` (or a jump). Words are big-endian. The payload is `ODLT` version 0's, so the library's
reader parses it; the type is `GEDL` so that frigate's factory
(`src/resource/factory/DisplayListFactory.cpp`) loads it and expands the
`G_SETTEX` requests (see Textures). Transcribed from `ResourceFactoryBinaryDisplayListV0` in
`external/libultraship-wiiu/src/fast/resource/factory/DisplayListFactory.cpp`.

A reference is a 16-byte command: a word pair, then `hash` (u64, as two
words) = CRC64 of the target path. The hash is libultraship's
`CRC64(const char*)`: CRC-64/ECMA-182 (polynomial `0x42f0e1eba9ea3693`,
MSB first), seed all ones, **no** final inversion.

| Source command | Emitted |
|---|---|
| `G_VTX` | `0x32` + hash. `w0` = `0x32<<24 \| count<<12 \| (first slot + count)<<1`; `w1` = byte offset of the first vertex within the target block |
| `G_DL` call | `0x31` + hash. `w0` = `0x31<<24`, `w1` = 0 |
| `G_DL` jump | as the call, with the branch flag kept in `w0` bit 16 |
| `G_SETTEX` | the original 8-byte word (rewritten at load time, see Textures), then `0x20` + hash naming the texture. `w0` = `0x20<<24 \| format<<21 \| size<<19 \| (width-1)` |
| `G_SETTIMG` | `0x20` + hash naming the embedded image; `w0` keeps the original format/size/width fields |
| `G_ENDDL` | unchanged |
| everything else (`G_MTX`, `G_MOVEMEM`, RDP and state commands, `G_TRI4`) | unchanged |

`G_MTX` and `G_MOVEMEM` keep their segment addresses: models address
matrices in segment 3, which the game fills at run time, so there is no
resource to name until the scene-graph walk exists.

## Textures

`G_SETTEX` (`0xc0`, the `G_NOOP` byte) is not RSP behavior. The original
game rewrites each request into RDP commands when it loads a list, so the
microcode never sees one; frigate does the same when the `GEDL` resource is
loaded (`src/texture/TexExpand.cpp`), from the referenced `Fast::Texture`.
Handled: the two tile types (`type` 3 and 4) with a
non-paletted image of 4, 8 or 16 bits. The image is loaded into TMEM 0
through tile 7, then tile 0 (and tile 1 for type 3) is set from the request's
wrap modes and the image's dimensions. A sample offset of 2 adds a half-texel
to the tile extent.

The interpreter takes the row stride from the `G_SETTIMG` width and size
fields, not from the tile, and a texture resource's rows are not padded to
whole TMEM words (a 4-bit row only to a whole byte). So the load describes the
image's real rows: 8- and 16-bit images load as themselves (`width x height`
texels of the image's own size) and the reference is kept as the packer wrote
it. A 4-bit image loads as bytes instead, `(width + 1) / 2` per row, with the
reference's size field rewritten to 8-bit and its width field to the row's
byte count: the interpreter's `G_LOADBLOCK` mishandles a 4-bit size, and its
12-bit count field is too small for a large 4-bit image's texels but holds
its bytes. Rendering the expanded result and comparing it with the source
image (`tools/verify_texture_render.py`) matches for 4-, 8- and 16-bit images,
including widths that are not a whole number of TMEM words. The interpreter
trims an odd-width 4-bit image to the tile width only when the tile clamps,
so the check requests clamped tiles.

Other requests (LOD, detail, mipmap, paletted or 32-bit images, unresolved
textures) are left as stored; the rest is tracked in
https://github.com/liz-baker/frigate/issues/29.

A `G_SETTEX` word holds the tile setup in `w0` and, in `w1`, a 12-bit
texture id (bits 0-11), a 12-bit detail texture id (bits 12-23) and a
minimum level (bits 24-31). The texture id is the index of the image in
`images.def`, so it selects `textures/<name>` directly; in the US ROM all
5,846 model uses match an entry in the file's own texture table with equal
dimensions. A nonzero detail id (about 180 bg uses) is recorded as a
reference but no command is emitted for it.

The format/size in the emitted `G_SETTIMG` come from the decoded image
(`TexFormat` → N64 format and size), since libultraship decodes a texture
resource by those fields. A paletted image also lists its `.pal` companion
as a reference; loading it is not emitted.

## Embedded images

The `G_SETTIMG` commands of three props (`Pgoldeneyelogo`, `Plegalpage`,
`Pnintendologo`; 30 uses, 8 distinct images) point at pixel data inside the
model file, at an address that is also a texture table entry's first word.
The image is the texels the following `G_LOADBLOCK` loads (its texel count
times the pixel size, which includes any mip levels) and is written as a
`Fast::Texture` resource with the N64 texels unchanged and no flags. Width
comes from the `G_SETTIMG`, height from the texel count. One image
(legal-page, format I, 16-bit) has no matching `Fast::TextureType`; its
type field is a placeholder, and decoding follows the command's own
format/size fields.

## Jumps

The US ROM's model and bg lists contain no `G_DL` jumps (every emitted list
ends in `G_ENDDL`, and bg lists have no `G_DL` at all), so the re-emitter's
handling of the flag is untested against real data. A jump is copied as a
`0x31` reference with the flag kept in `w0` bit 16. libultraship's
`G_DL_OTR_HASH` handler has no defined behavior for that form
(liz-baker/libultraship-wiiu#38), which matters only if a region's lists
use jumps.
