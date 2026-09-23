import struct
import tempfile
import unittest
from pathlib import Path

from ge_animation import (
    HEADER_SIZE,
    ROOT_HEADER_SIZE,
    RootMotionDescriptor,
    encode_animation_clip_payload,
    parse_animations,
)


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


class RootMotionTest(ParseAnimationsTest):
    # Two arrays. The first clip's descriptors/bitstream are the words the
    # second array holds, at byte offsets into the concatenation of both:
    # array `a` is 6 words (24 bytes), so `b`'s words start at byte 24.
    HEADER = (
        "u32 ANIM_DATA_a[] =\n"
        # 2 frames, width 8; stride 0x13 bits (word3 high half), frame size 32 bits;
        # descriptors at byte 24, bitstream at byte 36 (two 6-byte descriptors).
        "{ PTR_ANIM_ENTRY_a,0x00020800,0x00000018,0x00130020,0x00000024,0x00000000 };\n"
        "u32 ANIM_DATA_b[] =\n"
        # descriptor 0: bitOffset 0, bitCount 5, valueOffset 9; descriptor 1:
        # bitOffset 5, bitCount 0, valueOffset 0x0415; bitstream: 5 bytes
        # (2 frames * 19 bits, rounded up).
        "{ 0x00000500,0x00090005,0x00000415,0x11223344,0x55000000 };\n"
    )

    def test_reads_descriptors_and_bitstream_from_the_flattened_table(self):
        root = self._decomp(header_body=self.HEADER, entries_defines="#define PTR_ANIM_ENTRY_a 0x0\n",
                            entries_bin=bytes(8))

        clip = parse_animations(root)["a"]
        self.assertEqual(clip.root_stride_bits, 0x13)
        self.assertEqual(
            clip.root_descriptors,
            (RootMotionDescriptor(0, 5, 9), RootMotionDescriptor(5, 0, 0x0415)),
        )
        self.assertEqual(clip.root_bitstream, bytes.fromhex("1122334455"))

    def test_a_zero_stride_clip_keeps_its_descriptors_and_has_no_bitstream(self):
        header = (
            "u32 ANIM_DATA_a[] =\n"
            "{ PTR_ANIM_ENTRY_a,0x00020800,0x00000018,0x00000020,0x00000018,0x00000000 };\n"
            "u32 ANIM_DATA_b[] =\n"
            "{ 0x00000500,0x00090005,0x00000000,0x00000000,0x00000000,0x00000000 };\n"
        )
        root = self._decomp(header_body=header, entries_defines="#define PTR_ANIM_ENTRY_a 0x0\n",
                            entries_bin=bytes(8))

        clip = parse_animations(root)["a"]
        self.assertEqual(clip.root_stride_bits, 0)
        self.assertEqual(clip.root_bitstream, b"")


class EncodeAnimationClipPayloadTest(unittest.TestCase):
    def test_encodes_header_then_bitstream(self):
        from ge_animation import AnimationClip

        clip = AnimationClip(frame_count=2, angle_width_bits=16, loop=True, frame_size_bytes=12, bitstream=bytes(24))
        payload = encode_animation_clip_payload(clip)
        self.assertEqual(len(payload), HEADER_SIZE + 24 + ROOT_HEADER_SIZE)
        frame_count, angle_width_bits, loop, _padding, frame_size_bytes = struct.unpack_from(">IBBHI", payload)
        self.assertEqual(frame_count, 2)
        self.assertEqual(angle_width_bits, 16)
        self.assertEqual(loop, 1)
        self.assertEqual(frame_size_bytes, 12)
        self.assertEqual(payload[HEADER_SIZE : HEADER_SIZE + 24], bytes(24))

    def test_encodes_root_motion_after_the_bitstream(self):
        from ge_animation import AnimationClip

        clip = AnimationClip(
            frame_count=1,
            angle_width_bits=8,
            loop=False,
            frame_size_bytes=1,
            bitstream=b"\x07",
            root_stride_bits=13,
            root_descriptors=(RootMotionDescriptor(1, 5, 0x0102), RootMotionDescriptor(6, 7, 0x0304)),
            root_bitstream=b"\xaa\xbb",
        )
        payload = encode_animation_clip_payload(clip)
        root = payload[HEADER_SIZE + 1 :]
        stride, count, size = struct.unpack_from(">HHI", root)
        self.assertEqual((stride, count, size), (13, 2, 2))
        self.assertEqual(root[ROOT_HEADER_SIZE : ROOT_HEADER_SIZE + 10], bytes.fromhex("0001050102" "0006070304"))
        self.assertEqual(root[ROOT_HEADER_SIZE + 10 :], b"\xaa\xbb")


if __name__ == "__main__":
    unittest.main()
