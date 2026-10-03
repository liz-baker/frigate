#!/usr/bin/env python3
"""Builds a synthetic audio archive -- a sine tone as a sound-effect bank and
as an instrument, with two music tracks that play it -- for exercising the
audio path end to end without a ROM.

The tone is stored as GoldenEye ADPCM with an all-zero predictor book, which
makes the format a plain 4-bit PCM with a per-frame shift: a decoded sample is
its nibble shifted left by the frame's shift. It is crude, but a real
bank-file layout, so everything from the packer's `.ctl` parser to the audio
output sees the same structures a ROM's banks have.

Usage:
    python3 tools/o2r_packer/audio_fixture.py --output tone.o2r [--frequency 1000]
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
import tempfile
from pathlib import Path
from typing import List, Tuple

from ge_audio import (
    BANK_VERSION,
    MusicSequence,
    build_bank_payload,
    build_music_payload,
    build_sfx_names_payload,
    parse_bank_file,
)
from o2r_writer import O2rWriter

# The rate the synth plays a wave at unity pitch.
SYNTH_RATE = 22048
SAMPLES_PER_FRAME = 16
BYTES_PER_FRAME = 9

TONE_NAME = "TONE_SFX"

# The music tracks: number 0 is "none", then a short note that ends by itself
# and a note that loops forever. Both play the tone's key 72, an octave above
# its base key.
MUSIC_NAMES = ["M_NONE", "M_BLIP", "M_DRONE"]
MUSIC_KEY = 72
BLIP_SECONDS = 0.5
DIVISION = 96


def encode_adpcm_frames(samples: List[int]) -> bytes:
    """`samples` (padded to whole frames) as ADPCM against a zero book."""
    out = bytearray()
    for start in range(0, len(samples), SAMPLES_PER_FRAME):
        frame = samples[start : start + SAMPLES_PER_FRAME]
        frame += [0] * (SAMPLES_PER_FRAME - len(frame))
        shift = 0
        while shift < 12 and any(not -8 <= round(x / (1 << shift)) <= 7 for x in frame):
            shift += 1
        nibbles = [max(-8, min(7, round(x / (1 << shift)))) & 0xF for x in frame]
        out.append(shift << 4)
        for i in range(0, SAMPLES_PER_FRAME, 2):
            out.append((nibbles[i] << 4) | nibbles[i + 1])
    return bytes(out)


def tone_samples(frequency: float, seconds: float) -> List[int]:
    count = int(SYNTH_RATE * seconds)
    return [round(14000 * math.sin(2 * math.pi * frequency * i / SYNTH_RATE)) for i in range(count)]


def build_tone_bank(frequency: float = 1000.0, seconds: float = 1.0, playable_by_key: bool = False) -> Tuple[bytes, bytes]:
    """A `.ctl` and `.tbl` holding one sound: the tone, under a 20 ms attack,
    a 50 ms decay and a 50 ms release (so it ends by itself). With
    `playable_by_key` its key map covers every key and velocity, as a
    music instrument's must; otherwise it is a sound effect's parameters."""
    tbl = encode_adpcm_frames(tone_samples(frequency, seconds))

    ctl = bytearray(8)  # header: revision, bank count, one bank offset

    def add(blob: bytes) -> int:
        while len(ctl) % 4:
            ctl.append(0)
        offset = len(ctl)
        ctl.extend(blob)
        return offset

    envelope = add(struct.pack(">iiiBBxx", 20000, 50000, 50000, 127, 127))
    keymap = add(struct.pack(">BBBBBbxx", 0, 127 if playable_by_key else 0, 0, 127 if playable_by_key else 0, 60, 0))
    book = add(struct.pack(">ii", 2, 1) + bytes(32))
    wave = add(struct.pack(">IiBBxxII", 0, len(tbl), 0, 0, 0, book))
    sound = add(struct.pack(">IIIBBBx", envelope, keymap, wave, 64, 127, 0))
    instrument = add(struct.pack(">12BhH", 90, 64, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 200, 1) + struct.pack(">I", sound))
    bank = add(struct.pack(">hBBiI", 1, 0, 0, 22050, 0) + struct.pack(">I", instrument))
    struct.pack_into(">hhI", ctl, 0, BANK_VERSION, 1, bank)
    return bytes(ctl), tbl


def _varlen(value: int) -> bytes:
    parts = [value & 0x7F]
    value >>= 7
    while value:
        parts.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(parts))


def build_music_sequence(loop_forever: bool, ticks: int) -> bytes:
    """A compact sequence of one track playing `MUSIC_KEY` for `ticks`; with
    `loop_forever` the track repeats, otherwise it ends after the note."""
    track = bytearray()
    if loop_forever:
        track += _varlen(0) + bytes([0xFF, 0x2E, 0, 0])
    loop_start = 0x44 + len(track)
    track += _varlen(0) + bytes([0x90, MUSIC_KEY, 100]) + _varlen(ticks)
    if loop_forever:
        track += _varlen(ticks) + bytes([0xFF, 0x2D, 0xFF, 0xFF])
        track += struct.pack(">I", 0x44 + len(track) + 4 - loop_start)
        track += _varlen(0) + bytes([0xFF, 0x2F])
    else:
        track += _varlen(ticks) + bytes([0xFF, 0x2F])
    return struct.pack(">16I", 0x44, *([0] * 15)) + struct.pack(">I", DIVISION) + bytes(track)


def build_music_table() -> bytes:
    """The `GEMS` payload: M_NONE (a track that ends at once), then the blip
    and the drone."""
    ticks = int(BLIP_SECONDS * 1_000_000 / (500_000 / DIVISION))
    sequences = [build_music_sequence(False, 1), build_music_sequence(False, ticks), build_music_sequence(True, ticks)]
    return build_music_payload(
        0x100000, [MusicSequence(0x20000 + 0x100 * i, len(s), len(s), s) for i, s in enumerate(sequences)]
    )


def write_tone_archive(output: Path, frequency: float = 1000.0, seconds: float = 1.0) -> None:
    """Writes `audio/sfx`, `audio/sfx_names`, and the music resources
    (`audio/instruments`, `audio/music`, `audio/music_names`) the way
    pack_audio.py does."""
    ctl, tbl = build_tone_bank(frequency, seconds)
    payload = build_bank_payload(parse_bank_file(ctl, len(tbl)), tbl)
    music_ctl, music_tbl = build_tone_bank(frequency, seconds, playable_by_key=True)
    instruments = build_bank_payload(parse_bank_file(music_ctl, len(music_tbl)), music_tbl)
    with O2rWriter(output) as writer:
        writer.add_resource("audio/sfx", "GEAB", 0, 0, payload)
        # Sound 0 is "no sound"; the tone is sound 1.
        writer.add_resource("audio/sfx_names", "OBLB", 0, 0, build_sfx_names_payload(["NOTHING_SFX", TONE_NAME]))
        writer.add_resource("audio/instruments", "GEAB", 0, 0, instruments)
        writer.add_resource("audio/music", "GEMS", 0, 0, build_music_table())
        writer.add_resource("audio/music_names", "OBLB", 0, 0, build_sfx_names_payload(MUSIC_NAMES))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--frequency", type=float, default=1000.0)
    parser.add_argument("--seconds", type=float, default=1.0)
    args = parser.parse_args()
    write_tone_archive(args.output, args.frequency, args.seconds)
    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
