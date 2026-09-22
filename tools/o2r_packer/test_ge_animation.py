import struct
import tempfile
import unittest
from pathlib import Path

from ge_animation import HEADER_SIZE, encode_animation_clip_payload, parse_animations


class ParseAnimationsTest(unittest.TestCase):
    def _decomp(self, *, header_body, entries_defines, entries_bin):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        assets = root / "assets"
        assets.mkdir(parents=True)
        (assets / "animationtable_data.c").write_text(header_body)
        (assets / "animationtable_entries.h").write_text(entries_defines)
        (assets / "animationtable_entries.bin").write_bytes(entries_bin)
        return root

    def test_reads_header_fields_and_slices_the_entries_blob(self):
        # frame count 2 (0x0002), angle width 16 (0x10), loop flag 1 -> word1
        # = 0x00021001. Frame size 96 bits (0x0060) -> 12 bytes/frame -> word3
        # = 0x00000060 (unk0C left 0 since it's not used).
        header = (
            "u32 ANIM_DATA_walk[] =\n"
            "{\n"
            "    PTR_ANIM_ENTRY_walk,0x00021001,0x00000000,0x00000060,0x00000000,0x00000000\n"
            "};\n"
        )
        blob = bytes(range(24))  # 2 frames * 12 bytes
        root = self._decomp(header_body=header, entries_defines="#define PTR_ANIM_ENTRY_walk 0x0\n", entries_bin=blob)

        clips = parse_animations(root)
        clip = clips["walk"]
        self.assertEqual(clip.frame_count, 2)
        self.assertEqual(clip.angle_width_bits, 16)
        self.assertTrue(clip.loop)
        self.assertEqual(clip.frame_size_bytes, 12)
        self.assertEqual(clip.bitstream, blob[:24])

    def test_uses_the_named_entry_offset_into_the_shared_blob(self):
        header = (
            "u32 ANIM_DATA_a[] =\n"
            "{ PTR_ANIM_ENTRY_a,0x00010800,0x00000000,0x00000020,0,0 };\n"
            "u32 ANIM_DATA_b[] =\n"
            "{ PTR_ANIM_ENTRY_b,0x00010800,0x00000000,0x00000020,0,0 };\n"
        )
        # Each clip: 1 frame, angle width 8, frame size 4 bytes.
        blob = b"\xaa\xaa\xaa\xaa" + b"\xbb\xbb\xbb\xbb"
        entries = "#define PTR_ANIM_ENTRY_a 0x0\n#define PTR_ANIM_ENTRY_b 0x4\n"
        root = self._decomp(header_body=header, entries_defines=entries, entries_bin=blob)

        clips = parse_animations(root)
        self.assertEqual(clips["a"].bitstream, b"\xaa\xaa\xaa\xaa")
        self.assertEqual(clips["b"].bitstream, b"\xbb\xbb\xbb\xbb")

    def test_a_zero_frame_clip_has_an_empty_bitstream(self):
        header = "u32 ANIM_DATA_empty[] =\n{ 0x00000000,0x00000000,0x0000043b,0x00000000,0x00000000,0x00000000 };\n"
        root = self._decomp(header_body=header, entries_defines="", entries_bin=b"")

        clips = parse_animations(root)
        self.assertEqual(clips["empty"].frame_count, 0)
        self.assertEqual(clips["empty"].bitstream, b"")


class EncodeAnimationClipPayloadTest(unittest.TestCase):
    def test_encodes_header_then_bitstream(self):
        from ge_animation import AnimationClip

        clip = AnimationClip(frame_count=2, angle_width_bits=16, loop=True, frame_size_bytes=12, bitstream=bytes(24))
        payload = encode_animation_clip_payload(clip)
        self.assertEqual(len(payload), HEADER_SIZE + 24)
        frame_count, angle_width_bits, loop, _padding, frame_size_bytes = struct.unpack_from(">IBBHI", payload)
        self.assertEqual(frame_count, 2)
        self.assertEqual(angle_width_bits, 16)
        self.assertEqual(loop, 1)
        self.assertEqual(frame_size_bytes, 12)
        self.assertEqual(payload[HEADER_SIZE:], bytes(24))


if __name__ == "__main__":
    unittest.main()
