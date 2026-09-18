#!/usr/bin/env python3
"""Packs GoldenEye's texture bank (`images.def` + the already-split raw
texture files it indexes) into an O2R archive. Part of frigate issue #10.

Adapted from `goldeneye-pc-port`'s `scripts/make/generate_imagelist.py`
(MIT) -- see THIRD_PARTY_LICENSES.md. That script's job is figuring out
which named image goes with which split `.bin` file and how big it is,
by parsing `images.def`'s `IMAGE(NAME, SIZE, ...)` entries; this module
reuses exactly that format knowledge, but instead of writing an
offset/size CSV for a single concatenated ROM blob, it writes each
image's raw bytes as its own O2R resource, keyed by name, via
`O2rWriter` (see o2r_writer.py).

Note: this does not yet decode N64 texture pixel formats (CI4/CI8/IA8/
RGBA16/etc.) -- the split `.bin` files are packed as opaque raw payloads
under the generic Blob resource type. Once frigate issue #11 (runtime
resource types) lands a proper texture ResourceType, this should switch
to tagging these resources with that type instead, so a ResourceFactory
can actually decode them into GPU-uploadable pixel data.
"""

from __future__ import annotations

from pathlib import Path
from typing import List, NamedTuple, Union

from o2r_writer import O2rWriter


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


def pack_textures(
    images_def_path: Union[str, Path],
    split_dir: Union[str, Path],
    writer: O2rWriter,
    *,
    virtual_prefix: str = "textures",
    resource_type: str = "OBLB",
    version: int = 1,
) -> List[str]:
    """Reads every image named in `images.def` from `split_dir`
    (`<name>.bin`, matching the decomp/PC-port's own split-file
    convention) and writes it into `writer` at
    `<virtual_prefix>/<name>`.

    Returns the names of images that were missing their split `.bin`
    file (and so were skipped) -- callers should treat a non-empty
    result as a build error once real assets are wired in.
    """
    split_dir = Path(split_dir)
    missing: List[str] = []

    for entry in parse_images_def(images_def_path):
        bin_path = split_dir / f"{entry.name}.bin"
        if not bin_path.exists():
            missing.append(entry.name)
            continue

        payload = bin_path.read_bytes()
        # Content-hash-free placeholder ID: the real ID scheme (likely a
        # CRC64 of the virtual path, matching ArchiveManager::AddFileToVfs)
        # is a #11/runtime-integration decision, not a packer one -- 0
        # is a safe placeholder since ResourceLoader doesn't require Id to
        # be non-zero, only unique-if-used.
        writer.add_resource(
            f"{virtual_prefix}/{entry.name}",
            resource_type,
            version=version,
            resource_id=0,
            payload=payload,
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
