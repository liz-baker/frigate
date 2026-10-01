#!/usr/bin/env python3
"""Decodes a GoldenEye audio bank (a libultra `.ctl` bank file and its `.tbl`
sample table) into the `GEAB` resource payload. Layout of both:
docs/formats/goldeneye-audio-bank.md.

The `.ctl` links its structures by byte offset from the start of the file; the
payload links them by index into flat per-kind tables, so the runtime never
patches pointers. Structures the file shares (one envelope or wavetable
serving several sounds) stay shared.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Sequence

# The bank file's format revision: 'B1'.
BANK_VERSION = 0x4231

ADPCM_WAVE = 0
RAW16_WAVE = 1

NO_INDEX = 0xFFFFFFFF

# Wavetables the packer can describe. Anything else is a damaged or foreign
# file, since a decoded bank is never read at a guessed type.
_WAVE_TYPES = (ADPCM_WAVE, RAW16_WAVE)

# Largest ADPCM book the game's own books use (order 2, up to 16 predictors);
# a bound for rejecting a damaged count rather than a format limit.
_MAX_BOOK_WORDS = 2 * 16 * 8


class AudioBankError(ValueError):
    """Raised when a bank file does not decode."""


@dataclass
class Envelope:
    attack_time: int
    decay_time: int
    release_time: int
    attack_volume: int
    decay_volume: int


@dataclass
class KeyMap:
    velocity_min: int
    velocity_max: int
    key_min: int
    key_max: int
    key_base: int
    detune: int


@dataclass
class Book:
    order: int
    npredictors: int
    words: List[int]


@dataclass
class Loop:
    start: int
    end: int
    count: int
    state: List[int]


@dataclass
class WaveTable:
    # Byte offset into the sample table, and the data's length.
    base: int
    length: int
    type: int
    flags: int
    loop: int  # index into loops, or NO_INDEX
    book: int  # index into books, or NO_INDEX (raw waves have none)


@dataclass
class Sound:
    envelope: int
    keymap: int
    wavetable: int
    sample_pan: int
    sample_volume: int
    flags: int


@dataclass
class Instrument:
    volume: int
    pan: int
    priority: int
    flags: int
    trem_type: int
    trem_rate: int
    trem_depth: int
    trem_delay: int
    vib_type: int
    vib_rate: int
    vib_depth: int
    vib_delay: int
    bend_range: int
    sounds: List[int]


@dataclass
class Bank:
    sample_rate: int
    percussion: int  # index into instruments, or NO_INDEX
    instruments: List[int]  # indices into instruments; NO_INDEX for an empty slot


@dataclass
class AudioBank:
    envelopes: List[Envelope] = field(default_factory=list)
    keymaps: List[KeyMap] = field(default_factory=list)
    books: List[Book] = field(default_factory=list)
    loops: List[Loop] = field(default_factory=list)
    wavetables: List[WaveTable] = field(default_factory=list)
    sounds: List[Sound] = field(default_factory=list)
    instruments: List[Instrument] = field(default_factory=list)
    banks: List[Bank] = field(default_factory=list)


class _Reader:
    """Bounds-checked big-endian reads over the `.ctl` bytes."""

    def __init__(self, data: bytes):
        self.data = data

    def unpack(self, fmt: str, offset: int):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(self.data):
            raise AudioBankError(f"read of {size} bytes at {offset:#x} runs past the end of the bank file")
        return struct.unpack_from(fmt, self.data, offset)

    def u32(self, offset: int) -> int:
        return self.unpack(">I", offset)[0]


class _Interner:
    """Assigns each distinct source offset one index, decoding on first use."""

    def __init__(self, decode: Callable[[int], object], out: list):
        self._decode = decode
        self._out = out
        self._seen: Dict[int, int] = {}

    def index(self, offset: int) -> int:
        if offset not in self._seen:
            self._seen[offset] = len(self._out)
            self._out.append(self._decode(offset))
        return self._seen[offset]


def parse_bank_file(ctl: bytes, tbl_length: Optional[int] = None) -> AudioBank:
    """Decodes `ctl`. With `tbl_length`, also checks that every wavetable's
    sample data lies inside a sample table of that size."""
    r = _Reader(ctl)
    revision, bank_count = r.unpack(">hh", 0)
    if revision != BANK_VERSION:
        raise AudioBankError(f"bank file revision {revision:#x} is not 'B1'")
    if bank_count < 0:
        raise AudioBankError(f"bank count {bank_count} is negative")

    out = AudioBank()

    def decode_envelope(o: int) -> Envelope:
        attack, decay, release, attack_volume, decay_volume = r.unpack(">iiiBB", o)
        return Envelope(attack, decay, release, attack_volume, decay_volume)

    def decode_keymap(o: int) -> KeyMap:
        return KeyMap(*r.unpack(">BBBBBb", o))

    def decode_book(o: int) -> Book:
        order, npredictors = r.unpack(">ii", o)
        count = order * npredictors * 8
        if order < 0 or npredictors < 0 or count > _MAX_BOOK_WORDS:
            raise AudioBankError(f"ADPCM book at {o:#x} has implausible size (order {order}, {npredictors} predictors)")
        return Book(order, npredictors, list(r.unpack(f">{count}h", o + 8)))

    def decode_loop(o: int) -> Loop:
        start, end, count = r.unpack(">III", o)
        return Loop(start, end, count, list(r.unpack(">16h", o + 12)))

    envelopes = _Interner(decode_envelope, out.envelopes)
    keymaps = _Interner(decode_keymap, out.keymaps)
    books = _Interner(decode_book, out.books)

    def decode_raw_loop(o: int) -> Loop:
        # A raw wavetable's loop has no decoder state.
        start, end, count = r.unpack(">III", o)
        return Loop(start, end, count, [0] * 16)

    adpcm_loops = _Interner(decode_loop, out.loops)
    raw_loops = _Interner(decode_raw_loop, out.loops)

    def decode_wavetable(o: int) -> WaveTable:
        base, length, wave_type, flags = r.unpack(">IiBB", o)
        if wave_type not in _WAVE_TYPES:
            raise AudioBankError(f"wavetable at {o:#x} has unknown type {wave_type}")
        if length < 0:
            raise AudioBankError(f"wavetable at {o:#x} has negative length {length}")
        if tbl_length is not None and base + length > tbl_length:
            raise AudioBankError(f"wavetable at {o:#x} reads {base:#x}+{length:#x}, past the {tbl_length:#x}-byte sample table")
        loop_offset = r.u32(o + 12)
        if wave_type == ADPCM_WAVE:
            book_offset = r.u32(o + 16)
            if book_offset == 0:
                raise AudioBankError(f"ADPCM wavetable at {o:#x} has no book")
            book = books.index(book_offset)
            loop = adpcm_loops.index(loop_offset) if loop_offset else NO_INDEX
        else:
            book = NO_INDEX
            loop = raw_loops.index(loop_offset) if loop_offset else NO_INDEX
        return WaveTable(base, length, wave_type, flags, loop, book)

    wavetables = _Interner(decode_wavetable, out.wavetables)

    def decode_sound(o: int) -> Sound:
        envelope, keymap, wavetable, pan, volume, flags = r.unpack(">IIIBBB", o)
        for name, pointer in (("envelope", envelope), ("key map", keymap), ("wavetable", wavetable)):
            if pointer == 0:
                raise AudioBankError(f"sound at {o:#x} has no {name}")
        return Sound(
            envelopes.index(envelope), keymaps.index(keymap), wavetables.index(wavetable), pan, volume, flags
        )

    sounds = _Interner(decode_sound, out.sounds)

    def decode_instrument(o: int) -> Instrument:
        fields = r.unpack(">12Bhh", o)
        sound_count = fields[13]
        if sound_count < 0:
            raise AudioBankError(f"instrument at {o:#x} has negative sound count {sound_count}")
        sound_indices = []
        for i in range(sound_count):
            sound_offset = r.u32(o + 16 + 4 * i)
            if sound_offset == 0:
                raise AudioBankError(f"instrument at {o:#x} has a null sound {i}")
            sound_indices.append(sounds.index(sound_offset))
        return Instrument(*fields[:12], fields[12], sound_indices)

    instruments = _Interner(decode_instrument, out.instruments)

    for b in range(bank_count):
        bank_offset = r.u32(4 + 4 * b)
        if bank_offset == 0:
            raise AudioBankError(f"bank {b} has a null offset")
        inst_count, _flags, _pad, sample_rate, percussion = r.unpack(">hBBiI", bank_offset)
        if inst_count < 0:
            raise AudioBankError(f"bank {b} has negative instrument count {inst_count}")
        inst_indices = []
        for i in range(inst_count):
            inst_offset = r.u32(bank_offset + 12 + 4 * i)
            inst_indices.append(instruments.index(inst_offset) if inst_offset else NO_INDEX)
        out.banks.append(
            Bank(sample_rate, instruments.index(percussion) if percussion else NO_INDEX, inst_indices)
        )

    return out


def build_bank_payload(bank: AudioBank, tbl: bytes) -> bytes:
    """The `GEAB` payload: the flat tables, then the sample table."""
    out = bytearray()
    out += struct.pack(
        ">8I",
        len(bank.envelopes),
        len(bank.keymaps),
        len(bank.books),
        len(bank.loops),
        len(bank.wavetables),
        len(bank.sounds),
        len(bank.instruments),
        len(bank.banks),
    )
    for e in bank.envelopes:
        out += struct.pack(">iiiBBxx", e.attack_time, e.decay_time, e.release_time, e.attack_volume, e.decay_volume)
    for k in bank.keymaps:
        out += struct.pack(">BBBBBbxx", k.velocity_min, k.velocity_max, k.key_min, k.key_max, k.key_base, k.detune)
    for b in bank.books:
        out += struct.pack(">II", b.order, b.npredictors)
        out += struct.pack(f">{len(b.words)}h", *b.words)
    for loop in bank.loops:
        out += struct.pack(">III16h", loop.start, loop.end, loop.count, *loop.state)
    for w in bank.wavetables:
        out += struct.pack(">IIBBxxII", w.base, w.length, w.type, w.flags, w.loop, w.book)
    for s in bank.sounds:
        out += struct.pack(">IIIBBBx", s.envelope, s.keymap, s.wavetable, s.sample_pan, s.sample_volume, s.flags)
    for i in bank.instruments:
        out += struct.pack(
            ">12BhH",
            i.volume,
            i.pan,
            i.priority,
            i.flags,
            i.trem_type,
            i.trem_rate,
            i.trem_depth,
            i.trem_delay,
            i.vib_type,
            i.vib_rate,
            i.vib_depth,
            i.vib_delay,
            i.bend_range,
            len(i.sounds),
        )
        out += struct.pack(f">{len(i.sounds)}I", *i.sounds)
    for b in bank.banks:
        out += struct.pack(">III", b.sample_rate, len(b.instruments), b.percussion)
        out += struct.pack(f">{len(b.instruments)}I", *b.instruments)
    out += struct.pack(">I", len(tbl))
    out += tbl
    return bytes(out)


def pack_audio_bank(ctl: bytes, tbl: bytes) -> bytes:
    """The `GEAB` payload for one `.ctl`/`.tbl` pair."""
    return build_bank_payload(parse_bank_file(ctl, len(tbl)), tbl)
