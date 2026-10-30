#!/usr/bin/env python3
"""Runs the whole extraction check for the US ROM: Python packers -> reference archives, frigate-extract ->
extractor archives, tools/compare_archives.py between them.

usage: verify_extract.py <frigate-extract> <repo-root> [compare_archives.py options...]

Exits 77 (ctest SKIP_RETURN_CODE) when there is no US ROM in FRIGATE_ROMS, or the GoldenEye decomp the packers
read is not checked out."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77
ROM_SIZE = 12 * 1024 * 1024
PACKERS = ["textures", "objects", "geometry", "audio"]

extract_cli, root = sys.argv[1], Path(sys.argv[2])
compare_options = sys.argv[3:]


def candidates():
    for location in os.environ.get("FRIGATE_ROMS", "").split(os.pathsep):
        path = Path(location)
        if path.is_dir():
            yield from sorted(p for p in path.iterdir() if p.is_file())
        elif path.is_file():
            yield path


def find_us_rom(scratch):
    for path in candidates():
        if path.stat().st_size != ROM_SIZE:
            continue
        probe = subprocess.run([extract_cli, str(path), str(scratch / "probe"), "--region", "u"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if probe.returncode == 0:
            return path
    return None


def skip(reason):
    print("SKIP:", reason)
    sys.exit(SKIP)


if not (root / "external" / "007" / "src").is_dir():
    skip("the GoldenEye decomp is not checked out (just worktree-init all)")

with tempfile.TemporaryDirectory() as tmp:
    scratch = Path(tmp)
    rom = find_us_rom(scratch)
    if rom is None:
        skip("no US ROM in FRIGATE_ROMS")

    ref, ext = scratch / "ref", scratch / "ext"
    ref.mkdir()
    for name in PACKERS:
        subprocess.run([sys.executable, str(root / "tools/o2r_packer" / f"build_{name}_o2r.py"), "--rom", str(rom),
                        "--output", str(ref / f"goldeneye_{name}.o2r")], check=True)
    subprocess.run([extract_cli, str(rom), str(ext)], check=True)
    sys.exit(subprocess.run([sys.executable, str(root / "tools/compare_archives.py"), str(ref), str(ext),
                             "--covered", str(root / "tests/extract/covered_prefixes.txt"), *compare_options]).returncode)
