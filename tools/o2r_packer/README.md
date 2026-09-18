# O2R packer

Offline tool that packs GoldenEye's extracted assets into `.o2r` archives
for libultraship's resource system, per
[issue #10](https://github.com/liz-baker/frigate/issues/10) (part of the
asset-pipeline umbrella, [issue #5](https://github.com/liz-baker/frigate/issues/5)).

## Status

- `o2r_writer.py` -- the generic, game-agnostic foundation. Writes `.o2r`
  archives (plain zips) with per-resource headers in the exact binary
  layout libultraship's `ResourceLoader` expects. See
  [`docs/formats/o2r-resource-header.md`](../../docs/formats/o2r-resource-header.md)
  for the header/Blob-payload layout.
- `pack_textures.py` / `build_textures_o2r.py` -- packs the
  `images.def`-driven texture bank, adapting `goldeneye-pc-port`'s
  `IMAGE(NAME, SIZE, ...)` parsing (see `THIRD_PARTY_LICENSES.md`).
  `ge_texture.py` decodes GoldenEye's own texture-compression bitstream
  (Huffman/RLE/lookup/blur, plus the separately-paletted CI4/CI8 family)
  and each image is packed as a real `Fast::Texture` (`"OTEX"`) resource
  -- see
  [`docs/formats/goldeneye-texture-bitstream.md`](../../docs/formats/goldeneye-texture-bitstream.md)
  for the on-disk layout and
  [issue #18](https://github.com/liz-baker/frigate/issues/18) for the
  design discussion. Paletted images get a second `<name>.pal` companion
  resource holding just the palette; wiring a display list to actually
  reference it is issue #18's own step 3, still open. Model/geometry
  segments (below) remain opaque `Blob` payloads for now -- that's the
  rest of #18's scope.
- `pack_objects.py` / `build_objects_o2r.py` -- packs the
  `obseg/{chr,gun,prop}` object segments (character, gun, prop models) as
  opaque `Blob` payloads. Runtime deserialization into a real scene-graph
  is out of scope for issue #10 (see #12/#18).
- `pack_geometry.py` / `build_geometry_o2r.py` -- packs `obseg/{bg,stan,text}`
  (level geometry, collision, string tables) and font data, pulled from
  your own ROM rather than the decomp's checked-in source (see "Why
  bg/stan/text/font are pulled from your ROM" below).

All four drivers reuse the GoldenEye decomp's own extraction tooling
(`extract_baserom.u.sh`, `tools/extractor`) rather than reimplementing ROM
decompression.

## Building an archive from your own ROM

```sh
git submodule update --init external/007   # if not already checked out

python3 tools/o2r_packer/build_textures_o2r.py \
    --rom /path/to/your/baserom.u.z64 --output goldeneye_textures.o2r

python3 tools/o2r_packer/build_objects_o2r.py \
    --rom /path/to/your/baserom.u.z64 --output goldeneye_objects.o2r

python3 tools/o2r_packer/build_geometry_o2r.py \
    --rom /path/to/your/baserom.u.z64 --output goldeneye_geometry.o2r
```

Each driver symlinks your ROM into the decomp checkout, runs the decomp's
own extraction step for its asset category, then packs the result into
the named `.o2r`. All three need the tools the decomp's own extraction
needs (a C compiler for `tools/extractor`, `python3`) -- see the decomp's
own docs for anything platform-specific. No MIPS cross-toolchain needed.

## Usage

```python
from o2r_writer import O2rWriter

with O2rWriter("goldeneye.o2r") as writer:
    writer.add_resource(
        "chr/bond/Model",  # virtual path GoldenEye's game code looks this up by
        "OBLB",            # ResourceType 4-char code (see ResourceType.h)
        version=1,
        resource_id=some_content_hash,
        payload=raw_model_bytes,
    )
```

## Testing

```sh
python3 -m unittest test_o2r_writer -v
python3 -m unittest test_ge_texture -v
python3 -m unittest test_pack_textures -v
python3 -m unittest test_verify_roundtrip -v
python3 -m unittest test_build_textures_o2r -v
python3 -m unittest test_decomp_extract -v
python3 -m unittest test_pack_objects -v
python3 -m unittest test_build_objects_o2r -v
python3 -m unittest test_pack_geometry -v
python3 -m unittest test_build_geometry_o2r -v
```

No GoldenEye ROM or extracted assets are needed for any of the above --
these cover the archive/header, `images.def`/`filelist.u.csv` parsing,
and driver-script orchestration logic (the extraction subprocess itself
is stubbed) with synthetic fixtures, same rationale as
`tests/audio/AudioUcodeTest.cpp`.

## Validating the result against your own ROM

After building an archive against your own ROM, two independent things
are worth checking, and only the second is this tool's job:

- **Was the extraction itself correct?** That's the GoldenEye decomp's
  own job -- it already validates byte-exact extraction/recompilation via
  its pinned `ge007.*.sha1` hashes and `scripts/test_files.sh`. Run those
  per the decomp's own docs if you want to double-check the ROM data
  itself.
- **Does this repo's packer decode and pack that data consistently?**
  That's what `verify_roundtrip.py` checks -- for textures specifically,
  it decodes every image named in `images.def` and confirms each packed
  `Fast::Texture` resource's declared `ImageDataSize` matches what its
  own width/height/format imply (not a byte-for-byte diff against the
  source file: decoding is a real transformation now, not an opaque
  copy -- see `verify_roundtrip.py`'s module docstring):

  ```sh
  python3 tools/o2r_packer/verify_roundtrip.py textures \
      external/007/assets/images.def external/007/assets/images/split
  ```

  A clean run means every image decoded without error and produced a
  self-consistent payload -- it does not independently confirm the
  *pixels* are correct against real hardware output, which needs an
  actual GE ROM and a rendering comparison this repo doesn't have set up
  yet (see issue #6, on-hardware validation).

## Why bg/stan/text/font are pulled from your ROM, not built from source

The GoldenEye decomp checks its decompiled C source for these segments
into its own git history, and its Makefiles can compile that source back
into the exact same bytes. Building from that source would mean this
repo's own pipeline depends on level geometry, collision data, and text
committed to a vendored decompilation project's git history, rather than
on a ROM you supply yourself -- inconsistent with how every other asset
category here works, and with this repo's own "no Nintendo or Rare... ROM
data" claim about itself. So `build_geometry_o2r.py` extracts these from
your ROM instead, even though the decomp's own `filelist.u.csv` marks
them as not extracted by default.

## Why the ROM gets symlinked instead of passed as an argument

(Covers `build_textures_o2r.py`/`build_objects_o2r.py` specifically --
`build_geometry_o2r.py` doesn't use `extract_baserom.u.sh`, so this bug
doesn't apply to it.)

`extract_baserom.u.sh`'s own argument parsing has a pre-existing bug in
its two-argument form: `<mode> <rom>` falls through to a full extraction
instead of running just `<mode>`, because its two-argument branch's
condition can never select "images" mode. That's a bug in the vendored
decomp script itself, and it's harmless for `images` (the step still
produces correct output), just wasted time. The single-argument form
doesn't have this bug, provided the ROM is already at the default
`baserom.u.z64` path -- so `decomp_extract.py` symlinks your ROM there
and invokes the script with just the mode name.
