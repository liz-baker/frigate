#!/usr/bin/env python3
"""Unit tests for ge_audio.py, on synthetic bank files."""

import struct
import unittest

from ge_audio import (
    ADPCM_WAVE,
    BANK_VERSION,
    NO_INDEX,
    RAW16_WAVE,
    AudioBankError,
    build_bank_payload,
    build_sfx_names_payload,
    pack_audio_bank,
    parse_bank_file,
    parse_sfx_names,
)


class CtlBuilder:
    """Lays out a `.ctl` the way the file stores it: structures at byte
    offsets from the file start, linked by those offsets."""

    def __init__(self):
        self.data = bytearray()

    def add(self, blob: bytes) -> int:
        while len(self.data) % 4:
            self.data.append(0)
        offset = len(self.data)
        self.data += blob
        return offset

    def envelope(self, attack=1000, decay=2000, release=3000, attack_volume=127, decay_volume=100):
        return self.add(struct.pack(">iiiBBxx", attack, decay, release, attack_volume, decay_volume))

    def keymap(self, key_min=0, key_max=0x2F, key_base=60, detune=-3):
        return self.add(struct.pack(">BBBBBbxx", 0, 127, key_min, key_max, key_base, detune))

    def book(self, order=2, npredictors=1):
        words = list(range(order * npredictors * 8))
        return self.add(struct.pack(">ii", order, npredictors) + struct.pack(f">{len(words)}h", *words))

    def loop(self, start=16, end=160, count=-1 & 0xFFFFFFFF):
        return self.add(struct.pack(">III16h", start, end, count, *range(16)))

    def adpcm_wave(self, base, length, loop=0, book=0):
        return self.add(struct.pack(">IiBBxxII", base, length, ADPCM_WAVE, 0, loop, book))

    def raw_wave(self, base, length, loop=0):
        return self.add(struct.pack(">IiBBxxI", base, length, RAW16_WAVE, 0, loop))

    def sound(self, envelope, keymap, wavetable, pan=64, volume=100, flags=0):
        return self.add(struct.pack(">IIIBBBx", envelope, keymap, wavetable, pan, volume, flags))

    def instrument(self, sounds, volume=90, bend=200):
        header = struct.pack(">12BhH", volume, 64, 5, 0, 0, 1, 2, 3, 0, 4, 5, 6, bend, len(sounds))
        return self.add(header + struct.pack(f">{len(sounds)}I", *sounds))

    def bank(self, instruments, percussion=0, sample_rate=22050):
        header = struct.pack(">hBBiI", len(instruments), 0, 0, sample_rate, percussion)
        return self.add(header + struct.pack(f">{len(instruments)}I", *instruments))


def build_ctl(make):
    """Builds a `.ctl` whose structures `make(builder)` creates; `make`
    returns the bank offsets. The header is reserved first so offsets stay
    valid."""
    builder = CtlBuilder()
    builder.data += bytes(4 + 4 * 4)  # room for a header with up to 4 banks
    banks = make(builder)
    header = struct.pack(">hh", BANK_VERSION, len(banks)) + struct.pack(f">{len(banks)}I", *banks)
    builder.data[: len(header)] = header
    return bytes(builder.data)


def simple_ctl():
    def make(b):
        env = b.envelope()
        key = b.keymap()
        book = b.book()
        loop = b.loop()
        wave_a = b.adpcm_wave(0, 72, loop, book)
        wave_b = b.adpcm_wave(72, 36, 0, book)  # same book, no loop
        raw = b.raw_wave(108, 20)
        sounds = [
            b.sound(env, key, wave_a),
            b.sound(env, key, wave_b, pan=10),
            b.sound(env, key, raw, volume=50),
        ]
        inst = b.instrument(sounds)
        return [b.bank([inst])]

    return build_ctl(make)


class ParseBankTest(unittest.TestCase):
    def test_walks_the_whole_structure(self):
        bank = parse_bank_file(simple_ctl())
        self.assertEqual(len(bank.banks), 1)
        self.assertEqual(bank.banks[0].sample_rate, 22050)
        self.assertEqual(bank.banks[0].percussion, NO_INDEX)
        self.assertEqual(bank.banks[0].instruments, [0])
        self.assertEqual(len(bank.instruments), 1)
        inst = bank.instruments[0]
        self.assertEqual((inst.volume, inst.bend_range, inst.sounds), (90, 200, [0, 1, 2]))
        self.assertEqual(
            (inst.trem_type, inst.trem_rate, inst.trem_depth, inst.vib_type, inst.vib_delay), (0, 1, 2, 0, 6)
        )

    def test_shared_structures_are_stored_once(self):
        bank = parse_bank_file(simple_ctl())
        self.assertEqual(len(bank.envelopes), 1)
        self.assertEqual(len(bank.keymaps), 1)
        self.assertEqual(len(bank.books), 1)
        self.assertEqual(len(bank.wavetables), 3)
        self.assertEqual({s.envelope for s in bank.sounds}, {0})

    def test_decodes_fields(self):
        bank = parse_bank_file(simple_ctl())
        env = bank.envelopes[0]
        self.assertEqual((env.attack_time, env.decay_time, env.release_time), (1000, 2000, 3000))
        self.assertEqual((env.attack_volume, env.decay_volume), (127, 100))
        key = bank.keymaps[0]
        self.assertEqual((key.key_min, key.key_max, key.key_base, key.detune), (0, 0x2F, 60, -3))
        book = bank.books[0]
        self.assertEqual((book.order, book.npredictors, book.words), (2, 1, list(range(16))))
        wave = bank.wavetables[0]
        self.assertEqual((wave.base, wave.length, wave.type), (0, 72, ADPCM_WAVE))
        self.assertEqual(bank.loops[wave.loop].state, list(range(16)))
        self.assertEqual((bank.loops[wave.loop].start, bank.loops[wave.loop].end), (16, 160))
        self.assertEqual(bank.wavetables[1].loop, NO_INDEX)
        self.assertEqual(bank.sounds[1].sample_pan, 10)

    def test_raw_wave_has_no_book(self):
        bank = parse_bank_file(simple_ctl())
        raw = bank.wavetables[2]
        self.assertEqual((raw.type, raw.book, raw.loop), (RAW16_WAVE, NO_INDEX, NO_INDEX))

    def test_negative_attack_time_survives(self):
        # A decay time of -1 marks a sound that never decays by itself.
        def make(b):
            env = b.envelope(decay=-1)
            sound = b.sound(env, b.keymap(), b.adpcm_wave(0, 8, 0, b.book()))
            return [b.bank([b.instrument([sound])])]

        bank = parse_bank_file(build_ctl(make))
        self.assertEqual(bank.envelopes[0].decay_time, -1)

    def test_null_instrument_slot_and_percussion(self):
        def make(b):
            sound = b.sound(b.envelope(), b.keymap(), b.adpcm_wave(0, 8, 0, b.book()))
            inst = b.instrument([sound])
            return [b.bank([inst, 0], percussion=inst)]

        bank = parse_bank_file(build_ctl(make))
        self.assertEqual(bank.banks[0].instruments, [0, NO_INDEX])
        self.assertEqual(bank.banks[0].percussion, 0)

    def test_two_banks_share_instruments(self):
        def make(b):
            sound = b.sound(b.envelope(), b.keymap(), b.adpcm_wave(0, 8, 0, b.book()))
            inst = b.instrument([sound])
            return [b.bank([inst]), b.bank([inst], sample_rate=11025)]

        bank = parse_bank_file(build_ctl(make))
        self.assertEqual(len(bank.instruments), 1)
        self.assertEqual([b.sample_rate for b in bank.banks], [22050, 11025])

    def test_rejects_wrong_revision(self):
        ctl = bytearray(simple_ctl())
        ctl[0:2] = struct.pack(">h", 0x4232)
        with self.assertRaises(AudioBankError):
            parse_bank_file(bytes(ctl))

    def test_rejects_truncated_file(self):
        with self.assertRaises(AudioBankError):
            parse_bank_file(simple_ctl()[:40])

    def test_rejects_sample_data_past_the_table(self):
        with self.assertRaises(AudioBankError):
            parse_bank_file(simple_ctl(), tbl_length=100)
        parse_bank_file(simple_ctl(), tbl_length=128)

    def test_rejects_unknown_wave_type(self):
        def make(b):
            wave = b.add(struct.pack(">IiBBxxII", 0, 8, 7, 0, 0, 0))
            sound = b.sound(b.envelope(), b.keymap(), wave)
            return [b.bank([b.instrument([sound])])]

        with self.assertRaises(AudioBankError):
            parse_bank_file(build_ctl(make))


class PayloadTest(unittest.TestCase):
    def test_counts_tables_and_ends_with_the_samples(self):
        tbl = bytes(range(128))
        payload = pack_audio_bank(simple_ctl(), tbl)
        counts = struct.unpack_from(">8I", payload, 0)
        # envelopes, keymaps, books, loops, wavetables, sounds, instruments, banks
        self.assertEqual(counts, (1, 1, 1, 1, 3, 3, 1, 1))
        self.assertEqual(struct.unpack_from(">I", payload, len(payload) - 128 - 4)[0], 128)
        self.assertEqual(payload[-128:], tbl)

    def test_table_offsets_follow_the_documented_sizes(self):
        payload = pack_audio_bank(simple_ctl(), bytes(128))
        offset = 32
        offset += 16  # one envelope
        offset += 8  # one keymap
        offset += 8 + 16 * 2  # one order-2 book of 16 words
        offset += 44  # one loop
        wave = struct.unpack_from(">IIBBxxII", payload, offset)
        self.assertEqual(wave[:3], (0, 72, ADPCM_WAVE))
        self.assertEqual(wave[4], 0)  # first wave's book index
        offset += 20 * 3
        sound = struct.unpack_from(">IIIBBBx", payload, offset)
        self.assertEqual(sound, (0, 0, 0, 64, 100, 0))

    def test_empty_bank_round_trips_the_counts(self):
        payload = build_bank_payload(parse_bank_file(build_ctl(lambda b: [])), b"")
        self.assertEqual(struct.unpack(">8I", payload[:32]), (0,) * 8)
        self.assertEqual(payload[32:], struct.pack(">I", 0))


HEADER = """
typedef enum OTHER { A, B } OTHER;

typedef enum SFX_ID
{
    NOTHING_SFX,
    FIRST_SFX,            // a line comment, with a comma, in it
    /* a block
       comment */ SECOND_SFX,
    THIRD_SFX
} SFX_ID;
"""


class SfxNamesTest(unittest.TestCase):
    def test_names_are_indexed_by_value(self):
        self.assertEqual(parse_sfx_names(HEADER), ["NOTHING_SFX", "FIRST_SFX", "SECOND_SFX", "THIRD_SFX"])

    def test_explicit_values_leave_gaps_and_continue_from_there(self):
        text = "typedef enum SFX_ID { A_SFX, B_SFX = 3, C_SFX, D_SFX = 0x7 } SFX_ID;"
        self.assertEqual(parse_sfx_names(text), ["A_SFX", "", "", "B_SFX", "C_SFX", "", "", "D_SFX"])

    def test_missing_enum_is_reported(self):
        with self.assertRaises(AudioBankError):
            parse_sfx_names("typedef enum OTHER { A } OTHER;")

    def test_non_numeric_value_is_reported(self):
        with self.assertRaises(AudioBankError):
            parse_sfx_names("typedef enum SFX_ID { A_SFX = SOME_NAME } SFX_ID;")

    def test_payload_is_one_name_per_line(self):
        self.assertEqual(build_sfx_names_payload(["X", "", "Z"]), b"X\n\nZ")


if __name__ == "__main__":
    unittest.main()
