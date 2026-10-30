# Extraction verification

Every stage ported into `frigate_extract` (`docs/extract.md`) must produce the same archives as the Python packers in
`tools/o2r_packer`, byte for byte. `tools/compare_archives.py` checks that.

## Running it

    just verify-extract "romdata/GoldenEye 007 (USA).z64"
    just verify-extract "romdata/GoldenEye 007 (USA).z64" --prefix textures/

- `just ref-archives <rom>` builds the Python archives into `build/ref/` (the same packers as `pack_archives`; a full
  build takes about 20 s once the decomp's assets are extracted, longer the first time, so rerun `ext-archives`
  alone while iterating).
- `just ext-archives <rom>` builds only the `frigate-extract` target (no libultraship needed) and writes `build/ext/`.
- `just verify-extract <rom> [args]` does both, then runs `tools/compare_archives.py build/ref build/ext` with the
  manifest below; extra arguments go to the compare script.

## What the compare script does

It reads both sides as a set of `.o2r` files (or single files) and compares members by name, then by uncompressed
bytes, so zip timestamps and compression never matter. Member order is reported as a warning only: libultraship looks
members up by path. The output is a count table per top-level prefix (`textures/`, `models/`, ...), then for the first
`--limit` differing members the first differing byte, which header field or payload offset it falls in, the decoded
resource headers (`formats/o2r-resource-header.md`) and a hex dump of both sides.

| situation | strict (default) | `--allow-missing-in-ext` |
|-----------|------------------|--------------------------|
| bytes differ | failure | failure |
| member only in the extractor's archives | failure | failure |
| member only in the reference | failure | pending, unless under a covered prefix |

`--prefix` (repeatable) limits the comparison to members starting with it. The exit status is 1 on any failure.

## The flow

1. `just verify-extract <rom>` builds both archive sets and compares them strictly: every member of the Python
   packers' archives must be in the extractor's, byte for byte, and the extractor may add none. For the US ROM that is
   all 16202 reference members.
2. `just verify-texture-oracle <rom>` checks the texture decoder against the game's own code (below).

Both run as ctests, `VerifyExtract` and `ExtractTextureOracleTest`, which skip (exit 77) without a US ROM in
`FRIGATE_ROMS`, a path-separator list of ROM files and directories (`romdata/` is used when the variable is unset).
The Python packers read the decomp, so both need `just worktree-init all`.

Japan and Europe are not covered: no Python reference archives exist for them, so only the US ROM can be compared.

## Covered prefixes

`tests/extract/covered_prefixes.txt` lists the member prefixes `frigate-extract` produces. Every packer is ported, so
the `VerifyExtract` ctest runs strict. `--allow-missing-in-ext` with `--covered` remains for developing a new packer,
which is then pending until its prefixes are added:

1. Port the stage and register it in `RegisterDefaultStages()`.
2. Add its member prefixes to the manifest.
3. Run `just verify-extract <rom> --allow-missing-in-ext` until it reports no differing, missing or extra members.

## The game-code texture oracle

The Python and C++ texture decoders are checked independently against the decomp's `libpdtex` reference decoder
(`tools/o2r_packer/reference_compare.py`), but that decoder disagrees with the game on 236 non-paletted textures, which
it skips: the 74 IA4 textures, and 162 lookup-table textures (methods 6 and 7) in the formats where it indexes the
table differently. The game's code is the authority there, so `tests/extract/TextureOracleTest.cpp` runs it:

- `gen_game_texture_decoder.py` lifts the decomp's non-paletted decompression out of `src/game/image.c` and
  `src/game/image_bank.c` into a generated `game_texture_decoder.inc` in the build directory. It is never committed;
  CMake builds it only when `external/007`, python3 and a C compiler are present.
- `GameTextureOracle.c` wraps it with a small shim and calls `texInflateNonZlib` on each texture, for the base level
  only. The host adaptations are the entries in `PATCHES`: 64-bit pointer casts, the halfword byte the game reads as
  a table entry's low byte, and zeroed, enlarged scratch buffers (the game leaves them uninitialised, and its scratch
  buffer is too small for the larger IA4 alpha planes). Words are put back in big-endian order after the call.
- The test decodes every non-paletted texture in the ROM with both, and compares the tight rows (the game pads each
  row; the unused low nibble of an odd-width 4-bit row is masked). Paletted textures go through DEFLATE and are not
  covered, since the reference decoder already verifies them.

On the US ROM all 954 non-paletted textures match, including all 236 the reference decoder skips. One IA4 texture
(`2496`, a 64x64 blur-coded image) matches only at twice the row stride: the game's channel-plane path steps IA4
rows by `(width + 15) & ~15` bytes where every other path and format steps by half that, so the game's own copy has a
blank row between rows. The extractor emits the intended tight layout, and the test accepts the doubled stride for
that path.

## Tests

`tools/test_compare_archives.py` covers the script on synthetic archives (identical, content difference, missing,
extra, order only, prefix filter, pending mode); it runs as the `CompareArchivesTest` ctest, or
`cd tools && python3 -m unittest test_compare_archives`.
