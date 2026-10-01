#!/usr/bin/env python3
"""Unit tests for pack_ai_lists.py against a synthetic data segment."""

import struct
import unittest

import pack_ai_lists as p


def segment(first: bytes, second: bytes) -> bytes:
    """A data segment with two lists from the first list's address to the
    table's, and the table."""
    size = p.US_TABLE_ADDRESS - p.US_CDATA_LOAD_ADDRESS + 8 * 3
    data = bytearray(size)
    base = p.US_FIRST_LIST_ADDRESS - p.US_CDATA_LOAD_ADDRESS
    data[base : base + len(first)] = first
    data[base + len(first) : base + len(first) + len(second)] = second
    cursor = p.US_TABLE_ADDRESS - p.US_CDATA_LOAD_ADDRESS
    struct.pack_into(">II", data, cursor, p.US_FIRST_LIST_ADDRESS, 0)
    struct.pack_into(">II", data, cursor + 8, p.US_FIRST_LIST_ADDRESS + len(first), 1)
    return bytes(data)


class PackAiListsTest(unittest.TestCase):
    def test_reads_each_list_up_to_the_next(self):
        span = p.US_TABLE_ADDRESS - p.US_FIRST_LIST_ADDRESS
        first = b"\x02\x01\x04\x00"
        second = b"\x03\x04" + b"\x00" * (span - len(first) - 2)
        lists = p.read_global_lists(segment(first, second))
        self.assertEqual(lists, {0: first, 1: second})

    def test_text_resource_has_one_line_per_list(self):
        span = p.US_TABLE_ADDRESS - p.US_FIRST_LIST_ADDRESS
        data = segment(b"\x04\x00", b"\x04" + b"\x00" * (span - 3))
        lists = p.read_global_lists(data)
        self.assertEqual(sorted(lists), [0, 1])

    def test_rejects_a_table_that_is_not_where_expected(self):
        data = bytes(p.US_TABLE_ADDRESS - p.US_CDATA_LOAD_ADDRESS + 16)
        with self.assertRaises(p.AiListsError):
            p.read_global_lists(data)


if __name__ == "__main__":
    unittest.main()
