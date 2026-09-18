#!/usr/bin/env python3
"""Finds the user's own GoldenEye ROMs, by content rather than by
filename or location, for the opt-in ROM-backed checks (see
test_rom_textures.py).

A ROM is identified by SHA-1 against the hashes the GoldenEye decomp
already pins for each region (`ge007.<region>.sha1`), read at runtime
from the decomp checkout so there's no second copy to keep in sync.
Only big-endian `.z64` dumps match, same as the decomp itself.
"""

from __future__ import annotations

import hashlib
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, Optional

ROMS_ENV_VAR = "FRIGATE_ROMS"

# Anything outside this range can't be a GoldenEye dump; skipping it keeps
# discovery from hashing every large file in a directory of unrelated files.
_MIN_ROM_BYTES = 1 << 20
_MAX_ROM_BYTES = 64 << 20


@dataclass(frozen=True)
class Region:
    code: str
    # Mode passed to the decomp's extract_baserom script, or None if the
    # decomp has no image extraction for this region yet.
    image_extraction_mode: Optional[str]


# The decomp only ships image extraction (`extract_baserom.u.sh`,
# `imagelist.u.csv`) for the US ROM. Europe and Japan are recognised so
# their ROMs are found and reported, but skipped until that exists.
REGIONS: Dict[str, Region] = {
    "u": Region("u", image_extraction_mode="images"),
    "e": Region("e", image_extraction_mode=None),
    "j": Region("j", image_extraction_mode=None),
}


def expected_sha1(decomp_dir: Path, region: str) -> Optional[str]:
    """The decomp's pinned SHA-1 for `region`'s ROM, or None if its
    checkout has no hash file for that region."""
    hash_file = Path(decomp_dir) / f"ge007.{region}.sha1"
    if not hash_file.exists():
        return None
    return hash_file.read_text().split()[0].lower()


def _sha1_of(path: Path) -> str:
    digest = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _candidate_files(locations: Iterable[Path]) -> Iterable[Path]:
    for location in locations:
        if location.is_file():
            yield location
        elif location.is_dir():
            for entry in sorted(location.iterdir()):
                if entry.is_file():
                    yield entry


def discover_roms(locations: Iterable[Path], decomp_dir: Path) -> Dict[str, Path]:
    """Maps region code to the first file under `locations` (files, or
    directories searched one level deep) whose SHA-1 matches that
    region's pinned hash."""
    wanted = {}
    for region in REGIONS:
        sha1 = expected_sha1(decomp_dir, region)
        if sha1:
            wanted[sha1] = region

    found: Dict[str, Path] = {}
    for candidate in _candidate_files(locations):
        if not _MIN_ROM_BYTES <= candidate.stat().st_size <= _MAX_ROM_BYTES:
            continue
        region = wanted.get(_sha1_of(candidate))
        if region and region not in found:
            found[region] = candidate
    return found


def locations_from_env(environ=os.environ) -> list[Path]:
    """Parses `FRIGATE_ROMS`: an `os.pathsep`-separated list of ROM files
    and/or directories containing them."""
    raw = environ.get(ROMS_ENV_VAR, "")
    return [Path(p) for p in raw.split(os.pathsep) if p]
