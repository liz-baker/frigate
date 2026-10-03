#!/usr/bin/env python3
"""Packs GoldenEye's audio into the archive: the sound-effect bank and the
instrument bank as `GEAB` resources, each a libultra `.ctl` bank file with its
`.tbl` sample table (docs/formats/goldeneye-audio-bank.md), and the music
sequences as a `GEMS` resource (docs/formats/goldeneye-music-sequences.md).

All of these are plain ROM regions the decomp's scripts/filelist.u.csv lists
(`assets/music/<name>.ctl`/`.tbl`/`.sbk`), so offsets come from that file
rather than a second copy here.
"""

from __future__ import annotations

import csv
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from ge_audio import (
    AudioBankError,
    build_sfx_names_payload,
    pack_audio_bank,
    pack_music_sequences,
    parse_music_names,
    parse_sfx_names,
)
from o2r_writer import O2rWriter

RESOURCE_TYPE = "GEAB"
RESOURCE_VERSION = 0

# Where the sound-effect names are written: a Blob of one name per line, line
# `n` naming sound `n` (docs/formats/goldeneye-audio-bank.md).
SFX_NAMES_PATH = "audio/sfx_names"

# The music sequences and the track names, as the sound-effect names are
# (docs/formats/goldeneye-music-sequences.md).
MUSIC_PATH = "audio/music"
MUSIC_NAMES_PATH = "audio/music_names"
MUSIC_TYPE = "GEMS"
MUSIC_FILE = "music.sbk"

# (bank name, resource path). The instrument bank is packed with the same
# type so the music work reads it the same way.
BANKS: List[Tuple[str, str]] = [
    ("sfx", "audio/sfx"),
    ("instruments", "audio/instruments"),
]

_FILE_PREFIX = "assets/music/"


class AudioPackError(ValueError):
    """Raised when the ROM or file list does not hold an audio bank."""


def parse_audio_files(filelist_csv: Path) -> Dict[str, Tuple[int, int]]:
    """(ROM offset, size) of each `assets/music/*.ctl`/`.tbl` row, by file name."""
    files: Dict[str, Tuple[int, int]] = {}
    with open(filelist_csv, newline="") as f:
        for row in csv.reader(f):
            if len(row) < 3 or not row[2].startswith(_FILE_PREFIX):
                continue
            name = row[2][len(_FILE_PREFIX):]
            if name.endswith((".ctl", ".tbl")):
                files[name] = (int(row[0]), int(row[1]))
    return files


def read_music_file(rom: bytes, filelist_csv: Path) -> Tuple[int, bytes]:
    """The ROM offset and bytes of `music.sbk`, the sequence table and the
    compressed sequences after it."""
    with open(filelist_csv, newline="") as f:
        for row in csv.reader(f):
            if len(row) >= 3 and row[2] == _FILE_PREFIX + MUSIC_FILE:
                offset, size = int(row[0]), int(row[1])
                if offset + size > len(rom):
                    raise AudioPackError(f"ROM is too small to hold {MUSIC_FILE} ({offset:#x}+{size:#x})")
                return offset, rom[offset : offset + size]
    raise AudioPackError(f"{_FILE_PREFIX}{MUSIC_FILE} is not in the file list")


def read_bank_pair(rom: bytes, files: Dict[str, Tuple[int, int]], bank: str) -> Tuple[bytes, bytes]:
    """The `.ctl` and `.tbl` bytes of `bank` from `rom`."""
    parts = []
    for extension in ("ctl", "tbl"):
        name = f"{bank}.{extension}"
        if name not in files:
            raise AudioPackError(f"{_FILE_PREFIX}{name} is not in the file list")
        offset, size = files[name]
        if offset + size > len(rom):
            raise AudioPackError(f"ROM is too small to hold {name} ({offset:#x}+{size:#x})")
        parts.append(rom[offset : offset + size])
    return parts[0], parts[1]


def pack_audio(rom: bytes, filelist_csv: Path, writer: O2rWriter, constants_header: Optional[Path] = None) -> None:
    """Writes each bank to `audio/<bank>`, the music sequences to
    `audio/music`, and with `constants_header` (the decomp's
    `src/bondconstants.h`) the sound-effect and music track names too."""
    files = parse_audio_files(filelist_csv)
    for bank, path in BANKS:
        ctl, tbl = read_bank_pair(rom, files, bank)
        try:
            payload = pack_audio_bank(ctl, tbl)
        except AudioBankError as err:
            raise AudioPackError(f"{bank}: {err} (US ROM only)") from err
        writer.add_resource(path, RESOURCE_TYPE, RESOURCE_VERSION, 0, payload)

    rom_base, sbk = read_music_file(rom, filelist_csv)
    try:
        writer.add_resource(MUSIC_PATH, MUSIC_TYPE, RESOURCE_VERSION, 0, pack_music_sequences(sbk, rom_base))
    except AudioBankError as err:
        raise AudioPackError(f"music: {err} (US ROM only)") from err

    if constants_header is not None:
        try:
            header_text = constants_header.read_text()
            sfx_names = parse_sfx_names(header_text)
            music_names = parse_music_names(header_text)
        except AudioBankError as err:
            raise AudioPackError(f"{constants_header}: {err}") from err
        writer.add_resource(SFX_NAMES_PATH, "OBLB", 0, 0, build_sfx_names_payload(sfx_names))
        writer.add_resource(MUSIC_NAMES_PATH, "OBLB", 0, 0, build_sfx_names_payload(music_names))
