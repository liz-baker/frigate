# O2R packer

Offline tool that will pack GoldenEye's extracted assets into `.o2r`
archives for `libultraship-wiiu`'s resource system, per
[issue #10](https://github.com/liz-baker/frigate/issues/10) (part of the
asset-pipeline umbrella, [issue #5](https://github.com/liz-baker/frigate/issues/5)).

## Status

`o2r_writer.py` is the generic, game-agnostic foundation: it writes `.o2r`
archives (plain zips -- see its module docstring) and per-resource headers
in the exact binary layout `libultraship-wiiu`'s `ResourceLoader` expects,
transcribed from `ResourceLoader.cpp`/`ResourceType.h` rather than
copied from anywhere -- so it needs no `THIRD_PARTY_LICENSES.md` entry.
Every packer below writes its payloads as `OBLB` (`Blob`) resources, so
`add_resource()` also transparently adds the `uint32 dataSize` length
prefix `ResourceFactoryBinaryBlobV0::ReadResource()`
(`libultraship-wiiu/src/ship/resource/factory/BlobFactory.cpp`) expects
before a Blob's raw bytes -- callers only ever pass raw payload bytes in.

`pack_textures.py` packs the `images.def`-driven texture bank: it adapts
`goldeneye-pc-port`'s `IMAGE(NAME, SIZE, ...)` parsing (see
`THIRD_PARTY_LICENSES.md`) to write each named, already-split texture
file as its own O2R resource. It does not yet decode N64 pixel formats
(CI4/CI8/IA8/RGBA16/etc.) -- textures are packed as opaque `Blob`
payloads until issue #11 lands a real texture `ResourceType` to tag them
with.

`build_textures_o2r.py` is the one-command entry point: point it at your
own ROM and it drives the whole texture path end to end, shelling out to
`external/007`'s own extraction tooling (reuse, not reimplementation)
and handing the result straight to `pack_textures.py` -- you don't run
any decomp scripts yourself.

`pack_objects.py` packs the `obseg/{chr,gun,prop}` object segments
(character, gun, and prop models). Unlike the texture packer, it doesn't
adapt any of `goldeneye-pc-port`'s model-parsing scripts
(`generate_chr_c.py`/`generate_gun_c.py`/`generate_prop_model_c.py`) --
those decode a model's bytes into a C scene-graph for compiling into the
game binary, which is exactly the step O2R packaging replaces, and
runtime deserialization is explicitly out of scope for issue #10. It
packs each object's raw bytes as an opaque `Blob` payload instead, the
same placeholder approach `pack_textures.py` takes for pixel data, using
`external/007`'s own `scripts/filelist.u.csv` to know which files exist
per category (a bare format/fact, not adapted code, so no
`THIRD_PARTY_LICENSES.md` entry). `build_objects_o2r.py` is its
one-command driver, same shape as `build_textures_o2r.py`.

`pack_geometry.py` packs `obseg/{bg,stan,text}` (level geometry,
collision tiles, string tables) and font data. Like chr/gun/prop, these
are pulled straight from your own ROM -- *not* from `external/007`'s
checked-in decompiled C source, even though that source technically
already encodes this same data and its own Makefiles can compile it
back into these exact bytes. We deliberately don't build from that
source: it would mean this repo's own pipeline depends on data someone
else already committed to a vendored decompilation project's git
history, rather than on a ROM you supply, which doesn't match how every
other asset category here works (or frigate's README's "no Nintendo or
Rare... ROM data" claim about *this* repository). `external/007`'s own
`scripts/filelist.u.csv` already lists every one of these segments'
exact ROM offset/size -- their `extract` column just defaults to 0
there, since `extract_baserom.u.sh`'s default run skips them.
`build_geometry_o2r.py` doesn't touch that script at all: it feeds
`external/007`'s own `tools/extractor` binary (the same validated tool
`build_objects_o2r.py`'s underlying script already depends on) a
filtered copy of that CSV with `extract` forced to 1 for exactly these
rows. No MIPS toolchain needed -- just the same C compiler already
required to build `tools/extractor` for the other ROM-based drivers.

## Building a texture archive from your own ROM

```sh
git submodule update --init external/007   # if not already checked out

python3 tools/o2r_packer/build_textures_o2r.py \
    --rom /path/to/your/baserom.u.z64 \
    --output goldeneye_textures.o2r
```

This is this repo's own pipeline, not a manual decomp invocation: it
symlinks your ROM to `external/007/baserom.u.z64` and runs
`external/007/scripts/extract_baserom.u.sh images` itself (the decomp's
own image-extraction step, unmodified -- see below for why it's invoked
this way), then packs whatever comes out (`assets/images.def` +
`assets/images/split/`) into `goldeneye_textures.o2r`. Requires the
tools the decomp's own extraction needs (a C compiler for
`tools/extractor`, `python3`) -- see
`external/007`'s own docs for anything platform-specific.

## Building an object archive from your own ROM

```sh
git submodule update --init external/007   # if not already checked out

python3 tools/o2r_packer/build_objects_o2r.py \
    --rom /path/to/your/baserom.u.z64 \
    --output goldeneye_objects.o2r
```

Same shape as the texture driver: symlinks your ROM, runs
`external/007/scripts/extract_baserom.u.sh files`, then packs whatever
`chr`/`gun`/`prop` files come out into `goldeneye_objects.o2r`.

## Building a geometry archive (bg/stan/text/font) from your own ROM

```sh
git submodule update --init external/007   # if not already checked out

python3 tools/o2r_packer/build_geometry_o2r.py \
    --rom /path/to/your/baserom.u.z64 \
    --output goldeneye_geometry.o2r
```

Same ROM-based shape as the texture/object drivers, and the same
requirements (a C compiler to build `tools/extractor`) -- see "Why
bg/stan/text/font are pulled from your ROM, not built from source"
below for why this doesn't build from `external/007`'s checked-in
source the way its own Makefiles would.

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

After running `build_textures_o2r.py` (above) against your own ROM, two
independent things are worth checking, and only the second is this
tool's job:

- **Was the extraction itself correct?** That's `external/007`'s own
  job -- it already validates byte-exact extraction/recompilation via
  its pinned `ge007.*.sha1` (the final rebuilt ROM must match the
  original byte-for-byte) and `scripts/test_files.sh` (per-object-section
  md5s against known-good hashes). Run those per `external/007`'s own
  docs if you want to double-check the ROM data itself.
- **Does this repo's packer preserve that extracted data exactly when it
  packs it into an O2R archive and it's read back out?** That's what
  `verify_roundtrip.py` checks:

  ```sh
  python3 tools/o2r_packer/verify_roundtrip.py textures \
      external/007/assets/images.def external/007/assets/images/split
  ```

  It packs every texture named in `images.def`, reads each one back out
  of the resulting archive, and diffs the payload bytes against the
  original split file -- reporting any mismatch by name. A clean run
  means the packer isn't corrupting or truncating data; it does not by
  itself mean the *pixels* are correct once a real texture
  `ResourceType`/decoder exists (#11) -- only that packing is
  lossless.

## Why bg/stan/text/font are pulled from your ROM, not built from source

`external/007` checks its decompiled C source for these into its own
git history, and its Makefiles can compile that source back into the
exact same bytes these segments need. It would be less code to build
from that source than to extract it -- but doing so would mean this
repo's own pipeline depends on GoldenEye level geometry, collision
data, and text checked into a vendored decompilation project's git
history, rather than on a ROM you supply yourself. That doesn't match
how textures/chr/gun/prop already work here, or frigate's own README's
"no Nintendo or Rare... ROM data" claim about *this* repository -- so
`build_geometry_o2r.py` extracts these from your ROM instead, the same
way as everything else in this directory, even though `external/007`'s
own `scripts/filelist.u.csv` marks them as not extracted by default.

## Why the ROM gets symlinked instead of passed as an argument

(This section covers `build_textures_o2r.py`/`build_objects_o2r.py`
specifically -- `build_geometry_o2r.py` doesn't use
`extract_baserom.u.sh` at all, so this bug doesn't apply to it.)

`external/007/scripts/extract_baserom.u.sh`'s own argument parsing has a
pre-existing bug in its two-argument form: `<mode> <rom>` falls through
to a full extraction instead of running just `<mode>`, because its
two-argument branch's condition can never select "images" mode (and
would run the whole thing for "files" too, just via the coincidentally-
true `[ "$1" = "files" ]` clause rather than the intended one). That's a
bug in the vendored decomp script itself, so it isn't something to fix
in `external/007` from here (per this repo's `CLAUDE.md`) -- and it's
harmless for `images` (the images step that follows still produces
correct output), just wasted time. The script's single-argument form
doesn't have this bug, provided the ROM is already at the default
`baserom.u.z64` path it falls back to -- so `decomp_extract.py` (shared
by both `build_textures_o2r.py` and `build_objects_o2r.py`) symlinks
your ROM there and invokes the script with just the mode name, getting
the single-purpose extraction the script actually intends.
