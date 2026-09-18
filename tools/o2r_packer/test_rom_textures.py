#!/usr/bin/env python3
"""Opt-in end-to-end check of the texture pipeline against your own
GoldenEye ROMs. Skipped unless `FRIGATE_ROMS` points at them (see
rom_regions.py); no ROM is bundled or downloaded, and none is needed for
any other test in this package.

    FRIGATE_ROMS=/path/to/roms python3 -m unittest test_rom_textures -v

For each ROM found, per region:
  1. extracts its texture bank with the GoldenEye decomp's own tooling,
  2. checks every texture in `images.def` decodes and packs cleanly,
  3. compares each decoded texture against the decomp's own reference
     decoder (see reference_compare.py), if a C compiler is available.
"""

import collections
import tempfile
import unittest
import zipfile
from pathlib import Path

from decomp_extract import run_extraction_mode
from ge_texture import decode_texture
from o2r_writer import O2rWriter
from pack_textures import pack_textures, parse_images_def
from reference_compare import build_reference_dumper, compare, dump_reference
from rom_regions import REGIONS, ROMS_ENV_VAR, discover_roms, locations_from_env

DECOMP_DIR = Path(__file__).resolve().parents[2] / "external" / "007"


class RomTexturesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.roms = discover_roms(locations_from_env(), DECOMP_DIR)

    def setUp(self):
        if not self.roms:
            self.skipTest(f"set {ROMS_ENV_VAR} to your own ROM file(s) or directory to run this")

    def _extract(self, region: str):
        mode = REGIONS[region].image_extraction_mode
        if mode is None:
            raise unittest.SkipTest(f"the decomp has no image extraction for region {region!r} yet")
        run_extraction_mode(DECOMP_DIR, self.roms[region], mode)
        return DECOMP_DIR / "assets" / "images.def", DECOMP_DIR / "assets" / "images" / "split"

    def test_every_texture_packs(self):
        for region in REGIONS:
            if region not in self.roms:
                continue
            with self.subTest(region=region), tempfile.TemporaryDirectory() as tmp:
                images_def, split_dir = self._extract(region)
                archive = Path(tmp) / "textures.o2r"

                with O2rWriter(archive) as writer:
                    missing = pack_textures(images_def, split_dir, writer)

                self.assertEqual(missing, [])
                with zipfile.ZipFile(archive) as zf:
                    packed = {n for n in zf.namelist() if not n.endswith(".pal")}
                self.assertEqual(len(packed), len(parse_images_def(images_def)))

    def test_matches_reference_decoder(self):
        for region in REGIONS:
            if region not in self.roms:
                continue
            with self.subTest(region=region), tempfile.TemporaryDirectory() as tmp:
                images_def, split_dir = self._extract(region)
                dumper = Path(tmp) / "dump"
                if not build_reference_dumper(DECOMP_DIR, dumper):
                    self.skipTest("no C compiler available to build the reference decoder")

                verdicts = collections.Counter()
                mismatches = []
                for entry in parse_images_def(images_def):
                    path = split_dir / f"{entry.name}.bin"
                    ref = dump_reference(dumper, path, Path(tmp) / "out.bin")
                    if ref is None:
                        verdicts["reference could not decode"] += 1
                        continue
                    verdict = compare(decode_texture(path.read_bytes()), ref)
                    verdicts[verdict] += 1
                    if verdict == "mismatch":
                        mismatches.append(entry.name)

                print(f"\n[{region}] reference comparison: {dict(verdicts)}")
                self.assertEqual(mismatches, [], f"{len(mismatches)} texture(s) differ from the reference")
                self.assertGreater(verdicts["match"], 0)


if __name__ == "__main__":
    unittest.main()
