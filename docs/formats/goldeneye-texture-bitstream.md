# GoldenEye texture-bank bitstream

Decoded by `tools/o2r_packer/ge_texture.py` (frigate issue #18). Each
split `.bin` file under `assets/images/split/` is not raw N64 pixel
data — it's a custom-compressed bitstream of Rare's own design, MSB-first
bit-packed throughout.

Transcribed from the GoldenEye decomp's `src/game/image.c`/`image.h` and
cross-checked against the decomp's own vendored `tools/mktex`
("pd-tex") — both unlicensed upstream, so no prose or code is copied.

## Header (1 byte)

| bits | field | notes |
|------|-------|-------|
| 0 | has-explicit-LODs | unused by this decoder — see "Scope" below |
| 1 | is-paletted | selects the paletted or non-paletted branch |
| 2-7 | LOD count | unused by this decoder — ditto |

## Non-paletted branch

Per-image header, immediately following the top-level header:

| bits | field |
|------|-------|
| 0-3 | format (`TexFormat`, below) |
| 4-11 | width |
| 12-19 | height |
| 20-23 | compression method (`NonZlibCompression`, below) |

### Formats

| value | `TexFormat` | channels | 1-bit alpha plane | alphabet size | bits/pixel | native bytes | `Fast::TextureType` |
|-------|-------------|----------|--------------------|----------------|------------|---------------|----------------------|
| 0 | RGBA32 | R,G,B,A | no | 256 | 32 | 4B/px | RGBA32bpp |
| 1 | RGBA16 | R,G,B | yes | 32 | 16 | 2B/px 5551 BE | RGBA16bpp |
| 2 | RGB24 | R,G,B | no | 256 | 24 | 4B/px, A forced 0xff | RGBA32bpp |
| 3 | RGB15 | R,G,B | no | 32 | 15 | 2B/px 5551 BE, A forced 1 | RGBA16bpp |
| 4 | IA16 | I,A | no | 256 | 16 | 2B/px (I,A) | GrayscaleAlpha16bpp |
| 5 | IA8 | I,A | no | 16 | 8 | 1B/px (I4\<\<4\|A4) | GrayscaleAlpha8bpp |
| 6 | IA4 | I | yes | 8 | 4 | 2px/byte, nibble = I3\<\<1\|A1 | GrayscaleAlpha4bpp |
| 7 | I8 | I | no | 256 | 8 | 1B/px | Grayscale8bpp |
| 8 | I4 | I | no | 16 | 4 | 2px/byte, nibble = I4 | Grayscale4bpp |
| 9-12 | \*\_CI8/CI4 | — | — | — | — | paletted, see below | Palette8bpp/4bpp |

"Native bytes" is what this decoder emits as `DecodedTexture.pixel_data`
— GoldenEye's own N64-packed pixel layout, matching what libultraship's
`Interpreter::ImportTexture*` family (`interpreter.cpp`) reads a
`Fast::Texture`'s `ImageData` as directly, confirmed by reading those
functions rather than assumed from the `TextureType` enum's naming.
4-bit formats pack 2 pixels/byte, resetting to a fresh byte at the start
of each row (`ceil(width/2)` bytes/row) — an odd-width row's last nibble
is zero-padded, never carried into the next row.

### Compression methods

| value | name | decode |
|-------|------|--------|
| 0, 1 | UNCOMPRESSED0/1 | plain reads, one already-packed native pixel word per pixel — no channel planes (see below); nothing distinguishes the two IDs' decode behavior |
| 2 | HUFFMAN | one Huffman decode across all channels' planes back to back (`numChannels*width*height` symbols) |
| 3 | HUFFMAN_PER_CHANNEL | one Huffman decode per channel plane |
| 4 | RLE | one RLE decode across all channels' planes back to back |
| 5 | LOOKUP | a lookup table of native pixel words, indexed by a raw bitstream of indices |
| 6 | HUFFMAN_LOOKUP | same table, indices Huffman-decoded |
| 7 | RLE_LOOKUP | same table, indices RLE-decoded |
| 8 | HUFFMAN_BLUR | Huffman-decoded channel planes, blurred (below), then assembled |
| 9 | RLE_BLUR | RLE-decoded channel planes, blurred, then assembled |

Channel-plane methods (2-4, 8-9) decode `width*height` values per
channel, channel planes back to back in channel order (R,G,B,[A] /
I,[A]); when the format has a 1-bit alpha plane, it's a further
`width*height` 1-bit reads appended after the color channels, addressed
as if it were channel index 3 regardless of the format's real channel
count.

### Huffman decode

A per-symbol frequency table (`alphabetSize` bytes, one per symbol) is
read first, then a binary tree is built by repeatedly merging the two
lowest-frequency remaining nodes (ties/degenerate cases resolved the
same way every time — any decoder must reproduce this exact merge order
bit-for-bit, since it's rebuilding the same tree the original encoder
used to assign codes). Symbols are then decoded one bit at a time,
walking left (`0`) or right (`1`) from the root until reaching a leaf.

### RLE decode

A 3-field header (`back_bits`, `run_bits`, `value_bits`, 3+3+4 bits)
sets the field widths for what follows. A "fudge" constant is derived
from those widths (`cost = back_bits+run_bits+value_bits+1`, then
`fudge` counts how many times `value_bits+1` subtracts from `cost`
before it's `<= 0`). Each directive is one bit: `0` is a literal
(`value_bits` bits follow), `1` is a run — `back_bits` bits give a
back-distance, `run_bits` bits plus `fudge` give a run length, the run
copies that many already-decoded values from `back-distance` before the
current position, and is *always* followed by exactly one more literal
value with no directive bit of its own (the encoder never emits two
runs back to back — a longer run covers that case instead).

### Lookup table

An 11-bit entry count, then that many entries at the format's native
bits-per-pixel width each (so table entries are already-packed native
pixel words, not separate channel values). Index width is
`ceil(log2(numEntries))` bits — 0 bits for a single-entry table (a solid-colour texture), in which case no index bits are read at all.

### Blur post-process

Applied to a HUFFMAN_BLUR/RLE_BLUR image's decoded channel-plane values
(all channels treated as one `width` × `numChannels*height` block) before
native-byte assembly. A 3-bit method selects one of 7 neighbor-prediction
formulas; each stored value is a delta against that prediction, added
mod the format's alphabet size. The filter runs in scan order over one
array, so neighbors are the *already-reconstructed* values, not the
original deltas. The `/ 2` in methods 4-6 truncates toward zero (C
semantics). Out-of-bounds neighbors (at the left/top edges) read as 0.

## Paletted branch

| bits | field |
|------|-------|
| 0-7 | format — one of `RGBA16_CI8`/`RGBA16_CI4`/`IA16_CI8`/`IA16_CI4` |
| 8-15 | `numColours - 1` |
| 16-... | `numColours` palette entries, 16 bits each |
| next 8 | width |
| next 8 | height |
| (byte-aligned) | DEFLATE-compressed (RFC 1951) index stream |

Palette entries for the two `RGBA16_*` formats are RGBA5551, big-endian.
For the two `IA16_*` formats they're intensity8/alpha8; this decoder
normalizes them to RGBA5551 at pack time (`channel = intensity >> 3`
replicated into R/G/B, `alpha = 1` if `alpha8 >= 128` else `0`) —
libultraship's CI4/CI8 importers (`ImportTextureCi4`/`Ci8`,
`interpreter.cpp`) only implement the RGBA5551 TLUT interpretation, so
this is a documented lossy approximation, not a decode of real N64
IA16-TLUT hardware behavior, pending that runtime gap being closed.

The DEFLATE payload is prefixed by a 2-byte (`0x11 0x72`) or 5-byte
(`0x11 0x73 ...`) container header identifying "rarezip" — despite the
decomp's own "zlib" naming, the payload itself is plain raw DEFLATE, not
zlib-wrapped (confirmed against both the decomp's `rz_header_bytes`
table and mktex's `rzipInflate()`, which calls `inflateInit2(&strm,
-15)` — zlib's raw-deflate mode — on exactly this data).

The inflated index stream is already in GoldenEye's native N64-TMEM
packing: 1 byte/index for CI8, 2 nibble-packed indices/byte for CI4
(`ceil(width/2)` bytes/row, high nibble first) — confirmed against the
decomp's `texAlignIndices`, which copies these bytes through unchanged
(it only pads rows to an 8-byte boundary, which this tightly-packed
output doesn't need). This differs from `tools/mktex`'s own
`texAlignIndices`, which additionally unpacks CI4 to 1 byte/index for
that tool's own internal convenience — not evidence about the wire
format, since both agree on how many *source* bytes each output row
consumes.

## Scope: base image (LOD 0) only

GoldenEye's format can embed further, lower-resolution LOD images after
the first (when the "has explicit LODs" header bit is set), or have the
engine synthesize them at runtime from the base image (when it isn't) —
see the decomp's `texLoad()`/`texShrinkPaletted()`. Only the base image is
decoded; the header's LOD count and explicit bit are kept for the mip chain
the packer builds from it (`goldeneye-texture-mip-chain.md`). Decoding the
embedded levels is https://github.com/liz-baker/frigate/issues/458.
