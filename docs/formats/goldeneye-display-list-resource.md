# Re-emitted display lists (`Fast::DisplayList`)

Written by `tools/o2r_packer/ge_dlemit.py`. Input is a list stream as
described in `goldeneye-display-list.md`; output is one `ODLT` (version 0)
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

After the 20-byte header (`o2r-resource-header.md`): the ucode byte (6,
`ucode_indy_ge`), three zero bytes so commands start 8-byte aligned in the
file, then commands up to and including one whose opcode is `0xdf`. Words
are big-endian. Transcribed from `ResourceFactoryBinaryDisplayListV0` in
`external/libultraship-wiiu/src/fast/resource/factory/DisplayListFactory.cpp`.

A reference is a 16-byte command: a word pair, then `hash` (u64, as two
words) = CRC64 of the target path. The hash is libultraship's
`CRC64(const char*)`: CRC-64/ECMA-182 (polynomial `0x42f0e1eba9ea3693`,
MSB first), seed all ones, **no** final inversion.

| Source command | Emitted |
|---|---|
| `G_VTX` | `0x32` + hash. `w0` = `0x32<<24 \| count<<12 \| (first slot + count)<<1`; `w1` = byte offset of the first vertex within the target block |
| `G_DL` call | `0x31` + hash. `w0` = `0x31<<24`, `w1` = 0 |
| `G_DL` jump | the call above, then `G_ENDDL`: the hash form has no jump and a jump never returns |
| `G_SETTEX` | the original 8-byte word (unchanged, ignored by libultraship), then `0x20` + hash naming the texture. `w0` = `0x20<<24 \| format<<21 \| size<<19 \| (width-1)` |
| `G_SETTIMG` | `0x20` + hash naming the embedded image; `w0` keeps the original format/size/width fields |
| `G_ENDDL` | unchanged, followed by an unreachable `0xdf` word pair so the reader stops |
| everything else (`G_MTX`, `G_MOVEMEM`, RDP and state commands, `G_TRI4`) | unchanged |

`G_MTX` and `G_MOVEMEM` keep their segment addresses: models address
matrices in segment 3, which the game fills at run time, so there is no
resource to name until the scene-graph walk exists.

## Textures

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
