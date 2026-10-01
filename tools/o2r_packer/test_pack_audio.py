#!/usr/bin/env python3
"""Unit tests for pack_audio.py against a synthetic ROM (no ROM needed)."""

import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import OTR_HEADER_SIZE, O2rWriter, unpack_resource_header
from pack_audio import AudioPackError, pack_audio, parse_audio_files, read_bank_pair
from test_ge_audio import simple_ctl

CTL = simple_ctl()
TBL = bytes(range(128))


def make_rom_and_list(tmp: Path):
    """A ROM holding both banks and a file list naming them."""
    rom = bytearray(0x4000)
    layout = {}
    cursor = 0x100
    for bank in ("sfx", "instruments"):
        for extension, blob in (("ctl", CTL), ("tbl", TBL)):
            rom[cursor : cursor + len(blob)] = blob
            layout[f"{bank}.{extension}"] = (cursor, len(blob))
            cursor += len(blob) + 0x40
    rows = [f"{o},{s},assets/music/{n},0,1" for n, (o, s) in layout.items()]
    rows.insert(1, "999,4,assets/music/number_music_samples,0,0")
    rows.insert(0, "0,16,assets/other/thing.bin,0,1")
    csv_path = tmp / "filelist.csv"
    csv_path.write_text("\n".join(rows) + "\n")
    return bytes(rom), csv_path, layout


class ParseFilesTest(unittest.TestCase):
    def test_reads_only_ctl_and_tbl_rows(self):
        with tempfile.TemporaryDirectory() as tmp:
            _, csv_path, layout = make_rom_and_list(Path(tmp))
            self.assertEqual(parse_audio_files(csv_path), layout)

    def test_missing_file_is_reported(self):
        with self.assertRaises(AudioPackError):
            read_bank_pair(bytes(16), {}, "sfx")

    def test_rom_too_small_is_reported(self):
        with self.assertRaises(AudioPackError):
            read_bank_pair(bytes(16), {"sfx.ctl": (8, 100), "sfx.tbl": (0, 4)}, "sfx")


class PackAudioTest(unittest.TestCase):
    def test_writes_each_bank_as_a_geab_resource(self):
        with tempfile.TemporaryDirectory() as tmp:
            rom, csv_path, _ = make_rom_and_list(Path(tmp))
            archive = Path(tmp) / "audio.o2r"
            with O2rWriter(archive) as writer:
                pack_audio(rom, csv_path, writer)
            with zipfile.ZipFile(archive) as z:
                self.assertEqual(sorted(z.namelist()), ["audio/instruments", "audio/sfx"])
                data = z.read("audio/sfx")
            header = unpack_resource_header(data)
            self.assertEqual(header.resource_type, int.from_bytes(b"GEAB", "big"))
            payload = data[OTR_HEADER_SIZE:]
            self.assertEqual(struct.unpack_from(">8I", payload, 0), (1, 1, 1, 1, 3, 3, 1, 1))
            self.assertEqual(payload[-len(TBL) :], TBL)

    def test_a_damaged_bank_names_itself(self):
        with tempfile.TemporaryDirectory() as tmp:
            rom = bytearray(make_rom_and_list(Path(tmp))[0])
            _, csv_path, layout = make_rom_and_list(Path(tmp))
            rom[layout["sfx.ctl"][0] : layout["sfx.ctl"][0] + 2] = b"\0\0"
            with O2rWriter(Path(tmp) / "a.o2r") as writer:
                with self.assertRaisesRegex(AudioPackError, "sfx"):
                    pack_audio(bytes(rom), csv_path, writer)


if __name__ == "__main__":
    unittest.main()
