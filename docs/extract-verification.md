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

## Covered prefixes

`tests/extract/covered_prefixes.txt` lists the member prefixes `frigate-extract` produces. Every packer is ported, so
the `VerifyExtract` ctest (skipped without a US ROM in `FRIGATE_ROMS`/`romdata/`, the decomp checkout, or python3)
runs strict: all 16202 reference members must match. `--allow-missing-in-ext` with `--covered` remains for developing
a new packer, which is then pending until its prefixes are added:

1. Port the stage and register it in `RegisterDefaultStages()`.
2. Add its member prefixes to the manifest.
3. Run `just verify-extract <rom> --allow-missing-in-ext` until it reports no differing, missing or extra members.

## Tests

`tools/test_compare_archives.py` covers the script on synthetic archives (identical, content difference, missing,
extra, order only, prefix filter, pending mode); it runs as the `CompareArchivesTest` ctest, or
`cd tools && python3 -m unittest test_compare_archives`.
