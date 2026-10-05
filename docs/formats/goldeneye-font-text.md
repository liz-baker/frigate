# GoldenEye Font/Text on-disk layouts

Frigate-owned resource types with no libultraship analog — see
`src/resource/type/Font.h`/`Text.h` and their factories under
`src/resource/factory/`.

## Font (`assets/font/*.bin`, resource type `"GEFN"`)

Transcribed from the GoldenEye decomp's `struct font`/`struct fontchar`
(`src/bondtypes.h`) and the source form of a font bank
(`assets/font/fontBankGothic.c`'s `_kerning`/`_fontchartable`/`_fontbytes`
arrays) — only the bare struct-layout facts are used here, in fresh
types/wording.

Big-endian, matching the ROM's native MIPS byte order — the decomp never
byte-swaps these segments at runtime (`src/game/textrelated.c`'s
`load_font_tables()` is a straight DMA/memcpy):

| offset                              | field                                                                 |
|-------------------------------------|------------------------------------------------------------------------|
| 0                                   | kerning table, `kKerningTableSize` (13x13) big-endian s32 values       |
| `kKerningByteSize`                  | char table, `kNumChars` entries, 24 bytes each — 6 big-endian fields (`index`, `baseline`, `height`, `width`, `kerningIndex`, `pixelDataOffset`) |
| `kKerningByteSize + kNumChars * 24` | raw glyph pixel bytes                                                  |

Each char's `pixelDataOffset` points into the pixel region, file-relative
from the segment's own start (not from the pixel region's start — the
decomp's runtime fixup adds the segment's base pointer to this field
directly).

`kNumChars` is fixed at the NTSC glyph count (94); the decomp notes PAL
fonts use 135 instead. Not handled here since this port targets the USA
ROM (see the top-level README).

### Glyph pixels

Every glyph in the shipped USA banks has `index` < 0x80, which the decomp's
glyph renderer (`src/game/textrelated.c`, `textRenderGlyph`) draws as an
8-bit intensity texture (`G_IM_FMT_I`, `G_IM_SIZ_8b`), `height` rows of
`(width + 7) & ~7` bytes each, row-major. Indices of 0x80 and above take a
separate 4-bit paletted path fed from the Japanese glyph tables, not from
these banks.

Glyph data is laid out back to back from the first `pixelDataOffset`
(0xb80 in both banks, after alignment padding). Each glyph occupies
`stride * height` bytes followed by 8 more; the extra 8 are not part of the
image and are dropped on load. Measured across every glyph of both banks.

## Text (`assets/obseg/text/*`, resource type `"GETX"`)

Transcribed from the GoldenEye decomp's `src/game/language.c`
(`langGet()`'s offset resolution logic) rather than copied.

Big-endian, matching the ROM's native MIPS byte order — no runtime
byte-swap occurs; `langGet()` reads the offset table directly:

| offset | field |
|--------|-------|
| 0      | Up to `kOffsetTableSize` (1024) big-endian u32 offsets, one per string slot. `langGet()` indexes this table with `slotID & 0x3FF` (10 bits ⇒ exactly 1024 possible slots). A bank with fewer strings has a shorter table: it ends where the first string starts, and the slots past it have no string. |
| each entry | a nonzero offset is file-relative (from the segment's own start) to a NUL-terminated ASCII string elsewhere in the same segment; an offset of 0 means that slot has no string. |
