#!/usr/bin/env python3
"""Shared helper for driving the GoldenEye decomp's own
scripts/extract_baserom.u.sh against a caller-supplied ROM. Used by both
build_textures_o2r.py and build_objects_o2r.py so the workaround below
lives in exactly one place.

The script's own argument parsing has a bug in its two-argument form:
`<mode> <rom>` (e.g. "images <rom>") falls through to a full extraction
instead of running only `<mode>`, because the two-argument branch's
condition (`[ "$1" = "files" ] || [ "$1" != "images" ]`) can never select
"images" mode. That's a bug in the vendored decomp script itself, so it's
harmless for correctness (the requested step still runs and produces
correct output), just wasted extraction time. The single-argument form
doesn't have this bug, provided the ROM is already at the default
`baserom.u.z64` path it falls back to -- so we symlink the caller's ROM
there and invoke the script with just `<mode>`.
"""

from __future__ import annotations

import subprocess
from pathlib import Path


def run_extraction_mode(decomp_dir: Path, rom_path: Path, mode: str) -> None:
    """Runs the GoldenEye decomp's extract_baserom.u.sh in single-argument
    `mode` ("images" or "files") against `rom_path`. See module
    docstring for why it's invoked this way rather than `mode <rom>`."""
    script = decomp_dir / "scripts" / "extract_baserom.u.sh"
    if not script.exists():
        raise FileNotFoundError(
            f"{script} not found -- is the GoldenEye decomp submodule checked out? "
            "(git submodule update --init external/007)"
        )

    baserom_link = decomp_dir / "baserom.u.z64"
    if baserom_link.is_symlink() or baserom_link.exists():
        baserom_link.unlink()
    baserom_link.symlink_to(rom_path.resolve())

    subprocess.run(
        ["bash", str(script), mode],
        cwd=decomp_dir,
        check=True,
    )
