#!/usr/bin/env python3
"""Unit tests for compare_archives.py, on synthetic archives (no ROM needed)."""

import contextlib
import io
import tempfile
import unittest
import zipfile
from pathlib import Path

import compare_archives


def header(rtype=b"OBLB", version=0, rid=1):
    return (b"\x01\x00\x00\x00" + rtype + version.to_bytes(4, "big") + rid.to_bytes(8, "big")).ljust(64, b"\0")


def make(path, members, compression=zipfile.ZIP_DEFLATED):
    with zipfile.ZipFile(path, "w", compression) as archive:
        for name, data in members:
            archive.writestr(name, data)


BASE = [("textures/a", header() + b"aaaa"), ("textures/b", header(rid=2) + b"bbbb"), ("models/m", header() + b"mm")]


class CompareArchivesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        make(self.dir / "ref.o2r", BASE)

    def run_compare(self, members, *args, compression=zipfile.ZIP_DEFLATED):
        make(self.dir / "ext.o2r", members, compression)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = compare_archives.main([str(self.dir / "ref.o2r"), str(self.dir / "ext.o2r"), *args])
        return status, out.getvalue()

    def test_identical(self):
        status, out = self.run_compare(BASE)
        self.assertEqual(status, 0)
        self.assertIn("OK", out)

    def test_compression_and_timestamps_do_not_matter(self):
        status, _ = self.run_compare(BASE, compression=zipfile.ZIP_STORED)
        self.assertEqual(status, 0)

    def test_corrupted_member_fails_with_offset_and_header_decode(self):
        bad = list(BASE)
        bad[1] = ("textures/b", header(rid=2) + b"bXbb")
        status, out = self.run_compare(bad)
        self.assertEqual(status, 1)
        self.assertIn("DIFF textures/b: first difference at byte 65 (payload +1)", out)
        self.assertIn("OBLB v0 id=0x0000000000000002", out)

    def test_header_field_difference_is_named(self):
        bad = list(BASE)
        bad[0] = ("textures/a", header(version=3) + b"aaaa")
        status, out = self.run_compare(bad)
        self.assertEqual(status, 1)
        self.assertIn("header version", out)

    def test_missing_fails_in_strict_mode(self):
        status, out = self.run_compare(BASE[:2])
        self.assertEqual(status, 1)
        self.assertIn("MISSING: models/m", out)

    def test_missing_is_pending_when_allowed(self):
        status, out = self.run_compare(BASE[:2], "--allow-missing-in-ext")
        self.assertEqual(status, 0)
        self.assertIn("1 member(s) pending", out)

    def test_pending_mode_still_fails_on_difference_and_extra(self):
        bad = [("textures/a", header() + b"zzzz")]
        self.assertEqual(self.run_compare(bad, "--allow-missing-in-ext")[0], 1)
        extra = BASE[:2] + [("bg/x", b"x")]
        status, out = self.run_compare(extra, "--allow-missing-in-ext")
        self.assertEqual(status, 1)
        self.assertIn("EXTRA: bg/x", out)

    def test_extra_fails_in_strict_mode(self):
        self.assertEqual(self.run_compare(BASE + [("bg/x", b"x")])[0], 1)

    def test_covered_prefix_stays_strict_in_pending_mode(self):
        manifest = self.dir / "covered.txt"
        manifest.write_text("# comment\nmodels/  # trailing comment\n")
        status, out = self.run_compare(BASE[:2], "--allow-missing-in-ext", "--covered", str(manifest))
        self.assertEqual(status, 1)
        self.assertIn("MISSING: models/m", out)
        status, _ = self.run_compare(BASE[1:], "--allow-missing-in-ext", "--covered", str(manifest))
        self.assertEqual(status, 0)

    def test_order_only_difference_is_a_warning(self):
        status, out = self.run_compare(list(reversed(BASE)))
        self.assertEqual(status, 0)
        self.assertIn("different order", out)

    def test_prefix_filter(self):
        bad = [BASE[0], BASE[1], ("models/m", header() + b"XX")]
        self.assertEqual(self.run_compare(bad)[0], 1)
        status, out = self.run_compare(bad, "--prefix", "textures/")
        self.assertEqual(status, 0)
        self.assertNotIn("models/", out)
        self.assertEqual(self.run_compare(bad, "--prefix", "textures/", "--prefix", "models/")[0], 1)

    def test_directories_merge_archives(self):
        ref, ext = self.dir / "refdir", self.dir / "extdir"
        ref.mkdir()
        ext.mkdir()
        make(ref / "one.o2r", BASE[:1])
        make(ref / "two.o2r", BASE[1:])
        make(ext / "a.o2r", BASE[2:])
        make(ext / "b.o2r", BASE[:2])
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(compare_archives.main([str(ref), str(ext)]), 0)

    def test_summary_counts_per_prefix(self):
        _, out = self.run_compare(BASE[:2], "--allow-missing-in-ext")
        self.assertRegex(out, r"models/\s+0\s+0\s+0\s+1\s+0")
        self.assertRegex(out, r"textures/\s+2\s+0\s+0\s+0\s+0")


if __name__ == "__main__":
    unittest.main()
