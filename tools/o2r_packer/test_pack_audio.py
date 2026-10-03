#!/usr/bin/env python3
"""Unit tests for pack_audio.py against a synthetic ROM (no ROM needed)."""

import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import OTR_HEADER_SIZE, O2rWriter, unpack_blob_payload, unpack_resource_header
from pack_audio import AudioPackError, pack_audio, parse_audio_files, read_bank_pair
from test_ge_audio import HEADER, MUSIC_HEADER, compact_sequence, simple_ctl, simple_sbk

CTL = simple_ctl()
TBL = bytes(range(128))
SEQUENCES = [compact_sequence(), compact_sequence(96)]
SBK = simple_sbk(SEQUENCES)
SBK_OFFSET = 0x3000


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
    rom[SBK_OFFSET : SBK_OFFSET + len(SBK)] = SBK
    rows = [f"{o},{s},assets/music/{n},0,1" for n, (o, s) in layout.items()]
    rows.append(f"{SBK_OFFSET},{len(SBK)},assets/music/music.sbk,0,1")
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
                self.assertEqual(sorted(z.namelist()), ["audio/instruments", "audio/music", "audio/sfx"])
                data = z.read("audio/sfx")
            header = unpack_resource_header(data)
            self.assertEqual(header.resource_type, int.from_bytes(b"GEAB", "big"))
            payload = data[OTR_HEADER_SIZE:]
            self.assertEqual(struct.unpack_from(">8I", payload, 0), (1, 1, 1, 1, 3, 3, 1, 1))
            self.assertEqual(payload[-len(TBL) :], TBL)

    def test_writes_the_music_sequences_as_a_gems_resource(self):
        with tempfile.TemporaryDirectory() as tmp:
            rom, csv_path, _ = make_rom_and_list(Path(tmp))
            archive = Path(tmp) / "audio.o2r"
            with O2rWriter(archive) as writer:
                pack_audio(rom, csv_path, writer)
            with zipfile.ZipFile(archive) as z:
                data = z.read("audio/music")
            self.assertEqual(unpack_resource_header(data).resource_type, int.from_bytes(b"GEMS", "big"))
            payload = data[OTR_HEADER_SIZE:]
            self.assertEqual(struct.unpack_from(">II", payload, 0), (2, SBK_OFFSET))
            self.assertEqual(payload[-sum(len(s) for s in SEQUENCES) :], b"".join(SEQUENCES))

    def test_a_missing_music_file_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            rom, csv_path, _ = make_rom_and_list(Path(tmp))
            csv_path.write_text("".join(l + "\n" for l in csv_path.read_text().splitlines() if "music.sbk" not in l))
            with O2rWriter(Path(tmp) / "a.o2r") as writer:
                with self.assertRaisesRegex(AudioPackError, "music.sbk"):
                    pack_audio(rom, csv_path, writer)

    def test_writes_the_sound_names_as_a_blob(self):
        with tempfile.TemporaryDirectory() as tmp:
            rom, csv_path, _ = make_rom_and_list(Path(tmp))
            header = Path(tmp) / "bondconstants.h"
            header.write_text(HEADER + MUSIC_HEADER)
            archive = Path(tmp) / "audio.o2r"
            with O2rWriter(archive) as writer:
                pack_audio(rom, csv_path, writer, header)
            with zipfile.ZipFile(archive) as z:
                data = z.read("audio/sfx_names")
            header_fields = unpack_resource_header(data)
            self.assertEqual(header_fields.resource_type, int.from_bytes(b"OBLB", "big"))
            self.assertEqual(unpack_blob_payload(data[OTR_HEADER_SIZE:]), b"NOTHING_SFX\nFIRST_SFX\nSECOND_SFX\nTHIRD_SFX")
            names = unpack_blob_payload(zipfile.ZipFile(archive).read("audio/music_names")[OTR_HEADER_SIZE:])
            self.assertEqual(names, b"M_NONE\nM_FIRST\nM_SECOND")

    def test_a_header_without_the_enum_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            rom, csv_path, _ = make_rom_and_list(Path(tmp))
            header = Path(tmp) / "bondconstants.h"
            header.write_text("nothing here")
            with O2rWriter(Path(tmp) / "a.o2r") as writer:
                with self.assertRaisesRegex(AudioPackError, "SFX_ID"):
                    pack_audio(rom, csv_path, writer, header)

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
