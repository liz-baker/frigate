#!/usr/bin/env python3
"""Round-trip verification for the O2R packer, meant to be run against
real assets extracted from a user-supplied ROM (see README.md and the
GoldenEye decomp's own README for how to run its extraction scripts
first -- this repo doesn't wire that extraction step into its own build
yet).

This does NOT validate that extraction itself was correct -- that's the
GoldenEye decomp's own job, via its pinned `ge007.*.sha1` (final
rebuilt ROM, byte-exact) and `scripts/test_files.sh` (per-object-section
md5s against `scripts/make/build_hashtable.sh`'s known-good hashes, for
the recompile-to-C pipeline).

What this checks is narrower and specific to this repo's own new code.
For asset categories the packer stores as an opaque copy (chr/gun/prop
objects, bg/stan/text geometry -- see pack_objects.py/pack_geometry.py),
that means exactly what "round-trip" implies: packing an
already-extracted file into an O2R archive and reading it back out again
reproduces the exact same bytes.

Textures are different (frigate issue #18): `pack_textures.py` now
*decodes* each image (see ge_texture.py) rather than copying it
opaquely, so a byte-identical round-trip is neither expected nor
meaningful -- the packed payload is decoded pixel data, not a copy of
the compressed source bytes. `verify_textures_roundtrip()` instead
checks that every image in `images.def` decodes without error and that
its packed `Fast::Texture` payload's declared `ImageDataSize` matches
what its `width`/`height`/`texture_type` imply, catching decoder bugs
that silently truncate or misshape output without needing pixel-exact
reference data this repo doesn't have.

Usage:
    python3 verify_roundtrip.py textures <images.def path> <split dir>

Exits non-zero and prints every failure.
"""

from __future__ import annotations

import argparse
import struct
import sys
import tempfile
import zipfile
from pathlib import Path

from o2r_writer import O2rWriter, OTR_HEADER_SIZE
from pack_textures import pack_textures, parse_images_def

# Bits per pixel implied by each Fast::TextureType value this packer
# emits (see ge_texture.py's _TEXTURE_TYPE mapping) -- used only to
# sanity-check a packed texture's declared ImageDataSize below, not to
# duplicate ge_texture.py's own format tables.
_BITS_PER_PIXEL_BY_TEXTURE_TYPE = {
    1: 32,  # RGBA32bpp
    2: 16,  # RGBA16bpp
    3: 4,  # Palette4bpp
    4: 8,  # Palette8bpp
    5: 4,  # Grayscale4bpp
    6: 8,  # Grayscale8bpp
    7: 4,  # GrayscaleAlpha4bpp
    8: 8,  # GrayscaleAlpha8bpp
    9: 16,  # GrayscaleAlpha16bpp
}


def _expected_image_data_size(texture_type: int, width: int, height: int) -> int:
    bits_per_pixel = _BITS_PER_PIXEL_BY_TEXTURE_TYPE[texture_type]
    if bits_per_pixel < 8:
        pixels_per_byte = 8 // bits_per_pixel
        return ((width + pixels_per_byte - 1) // pixels_per_byte) * height
    return width * height * (bits_per_pixel // 8)


def verify_textures_roundtrip(images_def_path: str, split_dir: str) -> int:
    """Packs every image named in `images_def_path` and checks each
    packed `Fast::Texture` resource's declared size against what its
    own width/height/texture_type imply (see module docstring for why
    this isn't a byte-for-byte round trip). Returns the number of
    failures (0 = clean)."""
    split_dir_path = Path(split_dir)
    entries = parse_images_def(images_def_path)
    print(f"Found {len(entries)} images in {images_def_path}")

    failures = 0
    checked = 0

    with tempfile.TemporaryDirectory() as tmp:
        archive_path = Path(tmp) / "roundtrip.o2r"

        with O2rWriter(archive_path) as writer:
            missing = pack_textures(images_def_path, split_dir_path, writer)

        if missing:
            print(f"  (skipping {len(missing)} images with no split file present)")

        with zipfile.ZipFile(archive_path) as zf:
            for entry in entries:
                if entry.name in missing:
                    continue

                packed = zf.read(f"textures/{entry.name}")
                body = packed[OTR_HEADER_SIZE:]
                texture_type, width, height = struct.unpack(">III", body[:12])
                data_size = struct.unpack(">I", body[24:28])[0]

                checked += 1
                expected = _expected_image_data_size(texture_type, width, height)
                if data_size != expected:
                    failures += 1
                    print(
                        f"  MISMATCH: {entry.name} -- {width}x{height} texture_type={texture_type} "
                        f"declares {data_size} image bytes, expected {expected}"
                    )

    print(f"Checked {checked} textures, {failures} failure(s).")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    subparsers = parser.add_subparsers(dest="command", required=True)

    textures_parser = subparsers.add_parser("textures", help="Verify the texture-bank packer round-trips cleanly")
    textures_parser.add_argument("images_def", help="Path to images.def")
    textures_parser.add_argument("split_dir", help="Directory of split <name>.bin texture files")

    args = parser.parse_args()

    if args.command == "textures":
        mismatches = verify_textures_roundtrip(args.images_def, args.split_dir)
        return 1 if mismatches else 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
