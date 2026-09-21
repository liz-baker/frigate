#!/usr/bin/env python3
"""Unit tests for o2r_writer.py.

These need no GoldenEye ROM or extracted assets -- synthetic payloads only
-- so they run unconditionally in CI, same rationale as
tests/audio/AudioUcodeTest.cpp (see its header comment).
"""

import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import (
    Endianness,
    O2rWriter,
    OTR_HEADER_FIELDS_SIZE,
    OTR_HEADER_SIZE,
    pack_resource_header,
    resource_type_code,
    unpack_blob_payload,
    unpack_resource_header,
)

BLOB_LENGTH_PREFIX_SIZE = 4  # uint32 dataSize BlobFactory expects before a Blob's raw bytes.


class ResourceTypeCodeTest(unittest.TestCase):
    def test_matches_ResourceType_h_Blob_value(self):
        # ResourceType.h: Blob = 0x4F424C42 ("OBLB" read big-endian).
        self.assertEqual(resource_type_code("OBLB"), 0x4F424C42)

    def test_accepts_bytes_too(self):
        self.assertEqual(resource_type_code(b"OBLB"), resource_type_code("OBLB"))

    def test_rejects_wrong_length(self):
        with self.assertRaises(ValueError):
            resource_type_code("TOO_LONG")


class ResourceHeaderRoundTripTest(unittest.TestCase):
    def test_header_is_padded_to_libultraships_64_bytes(self):
        # Literal on purpose: this is libultraship's OTR_HEADER_SIZE, not just
        # whatever the writer's own constant says (see frigate issue #46).
        header = pack_resource_header("OBLB", version=0, resource_id=1)
        self.assertEqual(len(header), 64)
        self.assertEqual(header[OTR_HEADER_FIELDS_SIZE:], b"\x00" * (64 - OTR_HEADER_FIELDS_SIZE))

    def test_big_endian_round_trip(self):
        header = pack_resource_header("OBLB", version=3, resource_id=0x1122334455667788,
                                       byte_order=Endianness.BIG, is_custom=False)
        self.assertEqual(len(header), OTR_HEADER_SIZE)

        parsed = unpack_resource_header(header)
        self.assertEqual(parsed.byte_order, Endianness.BIG)
        self.assertFalse(parsed.is_custom)
        self.assertEqual(parsed.resource_type, resource_type_code("OBLB"))
        self.assertEqual(parsed.version, 3)
        self.assertEqual(parsed.resource_id, 0x1122334455667788)

    def test_little_endian_round_trip(self):
        header = pack_resource_header("OBLB", version=7, resource_id=0xDEADBEEF,
                                       byte_order=Endianness.LITTLE, is_custom=True)
        parsed = unpack_resource_header(header)
        self.assertEqual(parsed.byte_order, Endianness.LITTLE)
        self.assertTrue(parsed.is_custom)
        # The logical type/version/id values must decode the same
        # regardless of the on-disk byte order used to store them.
        self.assertEqual(parsed.resource_type, resource_type_code("OBLB"))
        self.assertEqual(parsed.version, 7)
        self.assertEqual(parsed.resource_id, 0xDEADBEEF)

    def test_field_layout_matches_ResourceLoader_cpp(self):
        # Mirrors ReadResourceInitDataBinary()'s exact read order:
        # byte order, is-custom, 2 reserved, u32 type, u32 version, u64 id.
        header = pack_resource_header("SHAD", version=1, resource_id=42, byte_order=Endianness.BIG)
        self.assertEqual(header[0], int(Endianness.BIG))
        self.assertEqual(header[1], 0)
        self.assertEqual(header[2:4], b"\x00\x00")
        self.assertEqual(struct.unpack(">I", header[4:8])[0], resource_type_code("SHAD"))
        self.assertEqual(struct.unpack(">I", header[8:12])[0], 1)
        self.assertEqual(struct.unpack(">Q", header[12:20])[0], 42)

    def test_rejects_short_buffer(self):
        with self.assertRaises(ValueError):
            unpack_resource_header(b"\x00" * (OTR_HEADER_FIELDS_SIZE - 1))


class O2rWriterTest(unittest.TestCase):
    def test_writes_plain_zip_with_headered_payload(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive_path = Path(tmp) / "test.o2r"
            payload = b"hello goldeneye"

            with O2rWriter(archive_path) as writer:
                writer.add_resource(
                    "chr/bond/Model", "OBLB", version=1, resource_id=0xABCD, payload=payload
                )

            # An O2R archive is a plain zip (O2rArchive.cpp: zip_open() +
            # index every non-directory entry by name) -- verify that's
            # exactly what we produced, with no extra container framing.
            with zipfile.ZipFile(archive_path) as zf:
                names = zf.namelist()
                self.assertEqual(names, ["chr/bond/Model"])

                raw = zf.read("chr/bond/Model")
                # Blob payloads carry an extra uint32 dataSize prefix
                # ResourceFactoryBinaryBlobV0::ReadResource() expects
                # (see o2r_writer.py's module docstring).
                self.assertEqual(len(raw), OTR_HEADER_SIZE + BLOB_LENGTH_PREFIX_SIZE + len(payload))

                parsed = unpack_resource_header(raw[:OTR_HEADER_SIZE])
                self.assertEqual(parsed.resource_type, resource_type_code("OBLB"))
                self.assertEqual(parsed.resource_id, 0xABCD)
                self.assertEqual(unpack_blob_payload(raw[OTR_HEADER_SIZE:]), payload)

    def test_blob_payload_length_prefix_matches_declared_byte_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive_path = Path(tmp) / "test.o2r"
            payload = b"little endian blob"

            with O2rWriter(archive_path) as writer:
                writer.add_resource(
                    "x", "OBLB", version=1, resource_id=1, payload=payload, byte_order=Endianness.LITTLE
                )

            with zipfile.ZipFile(archive_path) as zf:
                raw = zf.read("x")
                after_header = raw[OTR_HEADER_SIZE:]
                self.assertEqual(struct.unpack("<I", after_header[:4])[0], len(payload))
                self.assertEqual(
                    unpack_blob_payload(after_header, byte_order=Endianness.LITTLE), payload
                )

    def test_non_blob_resource_has_no_length_prefix(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive_path = Path(tmp) / "test.o2r"
            payload = b'{"not":"a blob"}'

            with O2rWriter(archive_path) as writer:
                writer.add_resource("x", "JSON", version=1, resource_id=1, payload=payload)

            with zipfile.ZipFile(archive_path) as zf:
                self.assertEqual(zf.read("x")[OTR_HEADER_SIZE:], payload)

    def test_multiple_resources_are_independently_addressable(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive_path = Path(tmp) / "test.o2r"
            with O2rWriter(archive_path) as writer:
                writer.add_resource("a", "OBLB", 1, 1, b"AAAA")
                writer.add_resource("b", "OBLB", 1, 2, b"BBBB")

            with zipfile.ZipFile(archive_path) as zf:
                self.assertEqual(sorted(zf.namelist()), ["a", "b"])
                self.assertEqual(unpack_blob_payload(zf.read("a")[OTR_HEADER_SIZE:]), b"AAAA")
                self.assertEqual(unpack_blob_payload(zf.read("b")[OTR_HEADER_SIZE:]), b"BBBB")

    def test_add_raw_file_has_no_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive_path = Path(tmp) / "test.o2r"
            with O2rWriter(archive_path) as writer:
                writer.add_raw_file("raw.bin", b"no header here")

            with zipfile.ZipFile(archive_path) as zf:
                self.assertEqual(zf.read("raw.bin"), b"no header here")

    def test_requires_context_manager(self):
        writer = O2rWriter("unused.o2r")
        with self.assertRaises(RuntimeError):
            writer.add_resource("x", "OBLB", 1, 1, b"")


if __name__ == "__main__":
    unittest.main()
