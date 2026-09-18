# O2R resource header

An `.o2r` archive is a plain zip file — `O2rArchive.cpp` opens it with
`zip_open()` and indexes every non-directory entry by its path. There is no
archive-level manifest; the structure that matters lives per-resource.

## Header layout

Every binary resource's bytes inside the zip are prefixed with this 20-byte
header, which `ResourceLoader::ReadResourceInitDataBinary()` reads before
handing the rest of the buffer to the matching `ResourceFactory`:

| offset | size | field                                                        |
|--------|------|--------------------------------------------------------------|
| 0      | 1    | byte order (1 = big-endian, 0 = little-endian)                |
| 1      | 1    | is-custom flag (0/1; alt-asset override marker)               |
| 2      | 2    | reserved (unused, written as zero)                             |
| 4      | 4    | resource type (big-endian uint32; 4-char ASCII code, e.g. `"OBLB"` for Blob) |
| 8      | 4    | resource version (big-endian uint32)                           |
| 12     | 8    | resource ID (big-endian uint64; content hash / unique ID)      |
| 20     | —    | payload begins here (`OTR_HEADER_SIZE`)                        |

Multi-byte header fields are written in the byte order the first header byte
declares, mirroring `BinaryReader::SetEndianness()` being applied before the
rest of the header is read.

Transcribed field-for-field from libultraship's own
`src/ship/resource/ResourceLoader.cpp` (`ReadResourceInitDataBinary`) — this
is libultraship's own documented format, not third-party code.

## Blob payloads

Blob resources (`ResourceType.Blob` / the `"OBLB"` fourcc) have one more
layer on top of the header, transcribed from
`ResourceFactoryBinaryBlobV0::ReadResource()`
(`src/ship/resource/factory/BlobFactory.cpp`): the bytes right after the
20-byte header are a `uint32 dataSize` (same byte order as the header),
followed by exactly that many raw bytes. The factory reads `dataSize`, then
that many bytes, then separately zero-pads its in-memory copy by 16 bytes for
N64 code that overreads slightly — that padding is a runtime detail, not
part of the archived payload.

`o2r_writer.py`'s `O2rWriter.add_resource()` adds this `uint32` prefix
automatically whenever `resource_type` is Blob/`OBLB`, so callers only ever
need to pass raw payload bytes.
