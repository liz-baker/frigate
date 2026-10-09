#!/usr/bin/env python3
"""Packs GoldenEye's texture bank (`images.def` + the already-split raw
texture files it indexes) into an O2R archive. Part of frigate issue #10.

`parse_images_def()` is adapted from `goldeneye-pc-port`'s
`scripts/make/generate_imagelist.py` (MIT) -- see
THIRD_PARTY_LICENSES.md. That script's job is figuring out which named
image goes with which split `.bin` file and how big it is, by parsing
`images.def`'s `IMAGE(NAME, SIZE, ...)` entries; this module reuses
exactly that format knowledge, but instead of writing an offset/size CSV
for a single concatenated ROM blob, it decodes each image (see
ge_texture.py, frigate issue #18) and writes the result as a real
`Fast::Texture` ("OTEX") O2R resource, keyed by name, via `O2rWriter`
(see o2r_writer.py).

Paletted (CI4/CI8) images additionally get a second, companion "OTEX"
resource at `<name>.pal` holding just the palette (as an
`Fast::TextureType::RGBA16bpp` image, `numcolours` pixels wide, 1 tall;
a CI8 palette is zero-padded to 256 entries)
-- see ge_texture.py's module docstring for why the palette can't live
in the same resource as the indexed pixel data.

An image the game mipmaps also gets a `<name>.mip` companion with its whole
chain, base level first, in the game's own memory layout (see ge_mipmap.py
and docs/formats/goldeneye-texture-mip-chain.md).
"""

from __future__ import annotations

import struct
from pathlib import Path
from typing import List, NamedTuple, Union

from ge_mipmap import build_mip_chain
from ge_texture import DecodedTexture, decode_texture
from o2r_writer import O2rWriter

_TEXTURE_RESOURCE_TYPE = "OTEX"
_PALETTE_TEXTURE_TYPE = 2  # Fast::TextureType::RGBA16bpp
_PALETTE_SUFFIX = ".pal"
_MIP_CHAIN_SUFFIX = ".mip"
_PALETTE8_TEXTURE_TYPE = 4  # Fast::TextureType::Palette8bpp
# libultraship's CI8 import reads a full 256-entry table, so a shorter CI8
# palette is zero-padded; indices past the real count never occur.
_CI8_PALETTE_ENTRIES = 256


def build_texture_v1_payload(
    texture_type: int,
    width: int,
    height: int,
    pixel_data: bytes,
    *,
    flags: int = 0,
    h_byte_scale: float = 1.0,
    v_pixel_scale: float = 1.0,
) -> bytes:
    """Builds a Texture (V1) resource payload. On-disk layout:
    docs/formats/o2r-resource-header.md."""
    return (
        struct.pack(">IIIIffI", texture_type, width, height, flags, h_byte_scale, v_pixel_scale, len(pixel_data))
        + pixel_data
    )


class ImageEntry(NamedTuple):
    name: str
    def_size: int


def parse_images_def(images_def_path: Union[str, Path]) -> List[ImageEntry]:
    """Parses `images.def`'s `IMAGE(NAME, SIZE, ...)` entries, in file
    order. Adapted from goldeneye-pc-port's parse_images_def() -- see
    module docstring."""
    entries: List[ImageEntry] = []

    with open(images_def_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("IMAGE("):
                continue

            content = line[len("IMAGE(") : -1]
            parts = [p.strip() for p in content.split(",", 2)]
            if len(parts) < 2:
                continue

            name = parts[0]
            size_str = parts[1]
            size = int(size_str, 16) if size_str.startswith("0x") else int(size_str)
            entries.append(ImageEntry(name=name, def_size=size))

    return entries


def _write_texture_resource(
    writer: O2rWriter,
    virtual_path: str,
    texture_type: int,
    width: int,
    height: int,
    pixel_data: bytes,
    version: int,
) -> None:
    payload = build_texture_v1_payload(texture_type, width, height, pixel_data)
    # Content-hash-free placeholder ID: the real ID scheme (likely a
    # CRC64 of the virtual path, matching ArchiveManager::AddFileToVfs)
    # is a #11/runtime-integration decision, not a packer one -- 0
    # is a safe placeholder since ResourceLoader doesn't require Id to
    # be non-zero, only unique-if-used.
    writer.add_resource(virtual_path, _TEXTURE_RESOURCE_TYPE, version=version, resource_id=0, payload=payload)


def pack_textures(
    images_def_path: Union[str, Path],
    split_dir: Union[str, Path],
    writer: O2rWriter,
    *,
    virtual_prefix: str = "textures",
    version: int = 1,
) -> List[str]:
    """Reads every image named in `images.def` from `split_dir`
    (`<name>.bin`, matching the decomp/PC-port's own split-file
    convention), decodes it (see ge_texture.py), and writes the result
    into `writer` at `<virtual_prefix>/<name>` as a real `Fast::Texture`
    resource (plus a `<name>.pal` companion for paletted formats -- see
    module docstring).

    Returns the names of images that were missing their split `.bin`
    file (and so were skipped) -- callers should treat a non-empty
    result as a build error once real assets are wired in. A `.bin`
    file that exists but fails to decode (`GeTextureFormatError`) is
    treated as a hard error, not a skip -- unlike a missing file, that
    means the extracted asset itself is corrupt or this decoder has a
    real gap, which callers shouldn't silently paper over.
    """
    split_dir = Path(split_dir)
    missing: List[str] = []

    for entry in parse_images_def(images_def_path):
        bin_path = split_dir / f"{entry.name}.bin"
        if not bin_path.exists():
            missing.append(entry.name)
            continue

        decoded: DecodedTexture = decode_texture(bin_path.read_bytes())

        _write_texture_resource(
            writer,
            f"{virtual_prefix}/{entry.name}",
            decoded.texture_type,
            decoded.width,
            decoded.height,
            decoded.pixel_data,
            version,
        )

        if decoded.palette_rgba5551 is not None:
            palette = decoded.palette_rgba5551
            if decoded.texture_type == _PALETTE8_TEXTURE_TYPE:
                palette = palette.ljust(_CI8_PALETTE_ENTRIES * 2, b"\0")
            num_colours = len(palette) // 2
            _write_texture_resource(
                writer,
                f"{virtual_prefix}/{entry.name}{_PALETTE_SUFFIX}",
                _PALETTE_TEXTURE_TYPE,
                num_colours,
                1,
                palette,
                version,
            )

        chain = build_mip_chain(decoded)
        if chain is not None:
            _write_texture_resource(
                writer,
                f"{virtual_prefix}/{entry.name}{_MIP_CHAIN_SUFFIX}",
                decoded.texture_type,
                decoded.width,
                decoded.height,
                b"".join(level.data for level in chain),
                version,
            )

    return missing


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser(description="Pack GoldenEye's texture bank into an O2R archive")
    parser.add_argument("images_def", help="Path to images.def")
    parser.add_argument("split_dir", help="Directory containing split <name>.bin texture files")
    parser.add_argument("output_o2r", help="Path to write the .o2r archive to")
    args = parser.parse_args()

    with O2rWriter(args.output_o2r) as writer:
        missing = pack_textures(args.images_def, args.split_dir, writer)

    if missing:
        print(f"Warning: {len(missing)} images missing split files, skipped:")
        for name in missing[:10]:
            print(f"  - {name}.bin")
        if len(missing) > 10:
            print(f"  ... and {len(missing) - 10} more")


if __name__ == "__main__":
    main()
