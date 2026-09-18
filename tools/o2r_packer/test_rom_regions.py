#!/usr/bin/env python3
"""Unit tests for rom_regions.py, against fake "ROMs" and a fake decomp
checkout holding their hashes -- no real ROM needed."""

import hashlib
import os
import tempfile
import unittest
from pathlib import Path

from rom_regions import REGIONS, ROMS_ENV_VAR, discover_roms, expected_sha1, locations_from_env

ROM_SIZE = (1 << 20) + 16


def _fake_rom(content_byte: bytes) -> bytes:
    return content_byte * ROM_SIZE


def _write_decomp_hashes(decomp_dir: Path, roms_by_region: dict) -> None:
    decomp_dir.mkdir(parents=True, exist_ok=True)
    for region, data in roms_by_region.items():
        digest = hashlib.sha1(data).hexdigest()
        (decomp_dir / f"ge007.{region}.sha1").write_text(f"{digest}  build/{region}/ge007.{region}.z64\n")


class ExpectedSha1Test(unittest.TestCase):
    def test_reads_first_token_of_hash_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp = Path(tmp)
            _write_decomp_hashes(decomp, {"u": _fake_rom(b"u")})
            self.assertEqual(expected_sha1(decomp, "u"), hashlib.sha1(_fake_rom(b"u")).hexdigest())

    def test_missing_hash_file_is_none(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIsNone(expected_sha1(Path(tmp), "u"))


class DiscoverRomsTest(unittest.TestCase):
    def test_identifies_region_by_content_not_filename(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp = Path(tmp) / "decomp"
            roms = {"u": _fake_rom(b"u"), "e": _fake_rom(b"e"), "j": _fake_rom(b"j")}
            _write_decomp_hashes(decomp, roms)

            rom_dir = Path(tmp) / "anywhere"
            rom_dir.mkdir()
            # Filenames deliberately don't say which region each is.
            (rom_dir / "a.bin").write_bytes(roms["j"])
            (rom_dir / "b.bin").write_bytes(roms["u"])
            (rom_dir / "unrelated.bin").write_bytes(_fake_rom(b"x"))

            found = discover_roms([rom_dir], decomp)

            self.assertEqual(found, {"j": rom_dir / "a.bin", "u": rom_dir / "b.bin"})

    def test_accepts_files_as_well_as_directories(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp = Path(tmp) / "decomp"
            rom = _fake_rom(b"e")
            _write_decomp_hashes(decomp, {"e": rom})
            rom_path = Path(tmp) / "eu.z64"
            rom_path.write_bytes(rom)

            self.assertEqual(discover_roms([rom_path], decomp), {"e": rom_path})

    def test_ignores_files_outside_plausible_rom_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp = Path(tmp) / "decomp"
            _write_decomp_hashes(decomp, {"u": b"tiny"})
            (Path(tmp) / "tiny.z64").write_bytes(b"tiny")

            self.assertEqual(discover_roms([Path(tmp)], decomp), {})

    def test_no_locations_finds_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(discover_roms([], Path(tmp)), {})

    def test_every_region_the_packer_knows_is_a_decomp_region(self):
        self.assertEqual(sorted(REGIONS), ["e", "j", "u"])


class LocationsFromEnvTest(unittest.TestCase):
    def test_splits_on_pathsep_and_drops_empties(self):
        env = {ROMS_ENV_VAR: os.pathsep.join(["/a", "", "/b/c.z64"])}
        self.assertEqual(locations_from_env(env), [Path("/a"), Path("/b/c.z64")])

    def test_unset_is_empty(self):
        self.assertEqual(locations_from_env({}), [])


if __name__ == "__main__":
    unittest.main()
