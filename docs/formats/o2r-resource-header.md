# O2R resource header

An `.o2r` archive is a plain zip file — `O2rArchive.cpp` opens it with
`zip_open()` and indexes every non-directory entry by its path. There is no
archive-level manifest; the structure that matters lives per-resource.

## Header layout

Every binary resource's bytes inside the zip are prefixed with a 64-byte
header (`OTR_HEADER_SIZE`, `include/ship/resource/archive/Archive.h`).
`ResourceLoader::ReadResourceInitDataBinary()` reads the first 20 bytes, laid
out below; the remaining 44 are reserved padding, written as zero. The loader
then positions the buffer at byte 64 (`ResourceLoader.cpp`) before handing the
rest to the matching `ResourceFactory`:

| offset | size | field                                                        |
|--------|------|--------------------------------------------------------------|
| 0      | 1    | byte order (1 = big-endian, 0 = little-endian)                |
| 1      | 1    | is-custom flag (0/1; alt-asset override marker)               |
| 2      | 2    | reserved (unused, written as zero)                             |
| 4      | 4    | resource type (big-endian uint32; 4-char ASCII code, e.g. `"OBLB"` for Blob) |
| 8      | 4    | resource version (big-endian uint32)                           |
| 12     | 8    | resource ID (big-endian uint64; content hash / unique ID)      |
| 20     | 44   | reserved padding (written as zero)                              |
| 64     | —    | payload begins here (`OTR_HEADER_SIZE`)                        |

Multi-byte header fields are written in the byte order the first header byte
declares, mirroring `BinaryReader::SetEndianness()` being applied before the
rest of the header is read.

The field layout is transcribed from libultraship's own
`src/ship/resource/ResourceLoader.cpp` (`ReadResourceInitDataBinary`), and the
payload offset from the same file's buffer positioning — this is libultraship's
own format, not third-party code. A payload written at byte 20 instead is read
44 bytes too far in (frigate issue #46).

## Blob payloads

Blob resources (`ResourceType.Blob` / the `"OBLB"` fourcc) have one more
layer on top of the header, transcribed from
`ResourceFactoryBinaryBlobV0::ReadResource()`
(`src/ship/resource/factory/BlobFactory.cpp`): the bytes right after the
header are a `uint32 dataSize` (same byte order as the header),
followed by exactly that many raw bytes. The factory reads `dataSize`, then
that many bytes, then separately zero-pads its in-memory copy by 16 bytes for
N64 code that overreads slightly — that padding is a runtime detail, not
part of the archived payload.

`o2r_writer.py`'s `O2rWriter.add_resource()` adds this `uint32` prefix
automatically whenever `resource_type` is Blob/`OBLB`, so callers only ever
need to pass raw payload bytes.

## Texture (V1) payloads

Texture resources (`Fast::ResourceType::Texture` / the `"OTEX"` fourcc)
carry this layout after the header, transcribed from
`ResourceFactoryBinaryTextureV1::ReadResource()`
(`external/libultraship-wiiu/src/fast/resource/factory/TextureFactory.cpp`):

| offset | size | field                                    |
|--------|------|-------------------------------------------|
| 0      | 4    | `Fast::TextureType` (uint32)               |
| 4      | 4    | width (uint32)                             |
| 8      | 4    | height (uint32)                            |
| 12     | 4    | flags (uint32; `TEX_FLAG_LOAD_AS_RAW`/`_IMG`) |
| 16     | 4    | `HByteScale` (float32)                     |
| 20     | 4    | `VPixelScale` (float32)                    |
| 24     | 4    | image data size (uint32)                   |
| 28     | —    | raw image bytes, that many                 |

Same byte order as the header. `tools/o2r_packer/pack_textures.py`'s
`build_texture_v1_payload()` builds this; see
`goldeneye-texture-bitstream.md` for what the image bytes themselves
look like per pixel format.
