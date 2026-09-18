#!/usr/bin/env python3
"""Opt-in check of the archives the object and geometry drivers write from
your own US ROM. Skipped unless `FRIGATE_ROMS` points at it (see
rom_regions.py).

    FRIGATE_ROMS=/path/to/roms python3 -m unittest test_rom_model_archives -v

Every reference inside every written display list must name a resource in
the two archives or in the texture bank.
"""

import tempfile
import unittest
import zipfile
from pathlib import Path

from build_geometry_o2r import build_geometry_o2r
from build_objects_o2r import build_objects_o2r
from ge_dlemit import iter_commands, path_hash
from o2r_writer import OTR_HEADER_SIZE
from pack_textures import parse_images_def
from rom_regions import ROMS_ENV_VAR, discover_roms, locations_from_env

DECOMP_DIR = Path(__file__).resolve().parents[2] / "external" / "007"


class RomModelArchiveTest(unittest.TestCase):
    def test_every_list_reference_resolves_within_the_archives(self):
        roms = discover_roms(locations_from_env(), DECOMP_DIR)
        if "u" not in roms:
            self.skipTest(f"set {ROMS_ENV_VAR} to your own US ROM file or directory to run this")

        with tempfile.TemporaryDirectory() as tmp:
            objects, geometry = Path(tmp) / "objects.o2r", Path(tmp) / "geometry.o2r"
            self.assertEqual(build_objects_o2r(DECOMP_DIR, roms["u"], objects), [])
            self.assertEqual(build_geometry_o2r(DECOMP_DIR, roms["u"], geometry), [])

            names = set()
            lists = []
            for archive in (objects, geometry):
                with zipfile.ZipFile(archive) as z:
                    for name in z.namelist():
                        names.add(name)
                        if "/dl/" in name:
                            lists.append((name, z.read(name)))
            for entry in parse_images_def(DECOMP_DIR / "assets" / "images.def"):
                names.update((f"textures/{entry.name}", f"textures/{entry.name}.pal"))

            known = {path_hash(name) for name in names}
            self.assertEqual(len(known), len(names), "path hash collision")
            self.assertGreater(len(lists), 4000)
            for name, blob in lists:
                for _, _, _, digest in iter_commands(blob[OTR_HEADER_SIZE:]):
                    if digest is not None:
                        self.assertIn(digest, known, name)


if __name__ == "__main__":
    unittest.main()
