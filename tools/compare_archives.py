#!/usr/bin/env python3
"""Compares two sets of O2R archives member by member, on uncompressed bytes.

Usage: compare_archives.py <ref> <ext> [options]

<ref> and <ext> are each an .o2r file or a directory of them (all *.o2r in it are merged by member name).
Zip timestamps, compression and member order never cause a difference; member order is reported as a warning
only, because libultraship indexes members by path and never depends on their order.

Members the reference has and the extractor lacks are failures, unless --allow-missing-in-ext is given: then they
are "pending", except under a prefix listed in the --covered manifest, which stays strict. Differing members and
members only the extractor has are always failures. Exit status is 0 on success, 1 on any failure, 2 on bad usage.
"""

import argparse
import struct
import sys
import zipfile
from pathlib import Path

HEADER_SIZE = 64  # docs/formats/o2r-resource-header.md
HEADER_FIELDS = [(0, 1, "byte order"), (1, 1, "is-custom"), (2, 2, "reserved"), (4, 4, "type"),
                 (8, 4, "version"), (12, 8, "resource id"), (20, 44, "padding")]


def load_members(path):
    """Returns ({member name: uncompressed bytes}, [member names in archive order])."""
    path = Path(path)
    files = sorted(path.glob("*.o2r")) if path.is_dir() else [path]
    if not files:
        raise SystemExit(f"error: no .o2r files in {path}")
    members, order = {}, []
    for file in files:
        with zipfile.ZipFile(file) as archive:
            for info in archive.infolist():
                if info.is_dir():
                    continue
                if info.filename in members:
                    print(f"warning: {info.filename} appears in more than one archive of {path}", file=sys.stderr)
                    continue
                members[info.filename] = archive.read(info)
                order.append(info.filename)
    return members, order


def top_prefix(name):
    head, sep, _ = name.partition("/")
    return head + "/" if sep else "(root)"


def read_manifest(path):
    prefixes = []
    for line in Path(path).read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            prefixes.append(line)
    return prefixes


def describe_header(data):
    if len(data) < 20:
        return "no resource header (%d bytes)" % len(data)
    endian = ">" if data[0] == 1 else "<"
    rtype = data[4:8] if endian == ">" else data[4:8][::-1]
    version, = struct.unpack(endian + "I", data[8:12])
    rid, = struct.unpack(endian + "Q", data[12:20])
    kind = rtype.decode("ascii", "replace")
    return "%s v%d id=0x%016x %s-endian custom=%d" % (kind, version, rid, "big" if endian == ">" else "little",
                                                      data[1])


def field_at(offset):
    if offset >= HEADER_SIZE:
        return "payload +%d" % (offset - HEADER_SIZE)
    for start, size, label in HEADER_FIELDS:
        if start <= offset < start + size:
            return "header %s" % label
    return "header"


def hexdump(data, offset, width=16):
    start = offset - offset % width
    return " ".join("%02x" % b for b in data[start:start + width]) + "  (from 0x%x)" % start


def first_difference(a, b):
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return i
    return min(len(a), len(b))


def report_difference(name, ref, ext):
    offset = first_difference(ref, ext)
    print(f"  DIFF {name}: first difference at byte {offset} ({field_at(offset)}), sizes {len(ref)} vs {len(ext)}")
    print(f"       ref: {describe_header(ref)}")
    print(f"       ext: {describe_header(ext)}")
    print(f"       ref {hexdump(ref, offset)}")
    print(f"       ext {hexdump(ext, offset)}")


KINDS = ("match", "differ", "missing", "pending", "extra")


def compare(ref, ref_order, ext, ext_order, prefixes, allow_missing, covered, limit):
    """Prints the report; returns (failure count, pending count)."""

    def selected(name):
        return not prefixes or any(name.startswith(p) for p in prefixes)

    def strict(name):
        return not allow_missing or any(name.startswith(p) for p in covered)

    counts = {}
    names = {"differ": [], "missing": [], "extra": []}
    for name in sorted(n for n in set(ref) | set(ext) if selected(n)):
        if name not in ext:
            kind = "missing" if strict(name) else "pending"
        elif name not in ref:
            kind = "extra"
        else:
            kind = "match" if ref[name] == ext[name] else "differ"
        counts.setdefault(top_prefix(name), dict.fromkeys(KINDS, 0))[kind] += 1
        if kind in names:
            names[kind].append(name)

    print("%-14s %8s %8s %8s %8s %8s" % (("prefix",) + KINDS))
    for prefix in sorted(counts):
        print("%-14s %8d %8d %8d %8d %8d" % ((prefix,) + tuple(counts[prefix][k] for k in KINDS)))
    totals = {k: sum(row[k] for row in counts.values()) for k in KINDS}
    print("%-14s %8d %8d %8d %8d %8d" % (("total",) + tuple(totals[k] for k in KINDS)))

    for name in names["differ"][:limit]:
        report_difference(name, ref[name], ext[name])
    for kind in ("missing", "extra"):
        for name in names[kind][:limit]:
            print(f"  {kind.upper()}: {name}")
    for kind in ("differ", "missing", "extra"):
        if len(names[kind]) > limit:
            print(f"  ... {len(names[kind]) - limit} more {kind} members")

    if [n for n in ref_order if n in ext and selected(n)] != [n for n in ext_order if n in ref and selected(n)]:
        print("warning: members are in a different order (harmless: the game looks members up by path)")

    return totals["differ"] + totals["missing"] + totals["extra"], totals["pending"]


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("ref", help="reference .o2r file or directory (the Python packers' output)")
    parser.add_argument("ext", help="extractor .o2r file or directory (frigate-extract's output)")
    parser.add_argument("--prefix", action="append", default=[], metavar="P",
                        help="only compare members whose name starts with P (repeatable)")
    parser.add_argument("--allow-missing-in-ext", action="store_true",
                        help="members only the reference has are pending, not failures")
    parser.add_argument("--covered", metavar="FILE",
                        help="manifest of prefixes the extractor produces; these stay strict under "
                             "--allow-missing-in-ext")
    parser.add_argument("--limit", type=int, default=10, help="members to detail per category (default 10)")
    args = parser.parse_args(argv)

    covered = read_manifest(args.covered) if args.covered else []
    ref, ref_order = load_members(args.ref)
    ext, ext_order = load_members(args.ext)
    failures, pending = compare(ref, ref_order, ext, ext_order, args.prefix, args.allow_missing_in_ext, covered,
                                args.limit)
    if failures:
        print(f"FAIL: {failures} member(s) differ, are missing or are extra")
        return 1
    print("OK" + (f" ({pending} member(s) pending)" if pending else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
