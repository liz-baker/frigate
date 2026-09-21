#!/usr/bin/env python3
"""Packs libultraship's Fast3D shader sources into `f3d.o2r`.

The rendering backends load their shaders through the ResourceManager
(`shaders/<api>/default.shader.<ext>`) and abort on the first draw that
needs one if no mounted archive has them. libultraship ships the sources
but leaves packaging to the consuming application.

Unlike the other drivers this needs no ROM: the inputs are the shader
sources under libultraship's `src/fast/shaders`.
"""

import argparse
import sys
from pathlib import Path

from o2r_writer import O2rWriter

SHADER_TYPE = "SHAD"
SHADER_VERSION = 0


def build_f3d_o2r(shaders_dir: Path, output: Path) -> int:
    """Writes every file under `shaders_dir` as a Shader resource at
    `shaders/<relative path>`. Returns the number of shaders written."""
    files = sorted(p for p in shaders_dir.rglob("*") if p.is_file())
    if not files:
        raise FileNotFoundError(f"no shader sources under {shaders_dir}")

    with O2rWriter(output) as writer:
        for index, path in enumerate(files):
            virtual_path = "shaders/" + path.relative_to(shaders_dir).as_posix()
            # No terminator: ReadCString stops at end of data, and would keep a
            # NUL in the returned text, which the shader preprocessor rejects.
            payload = path.read_bytes()
            writer.add_resource(virtual_path, SHADER_TYPE, SHADER_VERSION, index, payload)
    return len(files)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shaders", required=True, type=Path, help="libultraship src/fast/shaders directory")
    parser.add_argument("--output", required=True, type=Path, help="f3d.o2r to write")
    args = parser.parse_args(argv)

    count = build_f3d_o2r(args.shaders, args.output)
    print(f"wrote {count} shaders to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
