#!/usr/bin/env python3
"""Writes the same resources with the C++ writer (via the WriterFixture binary) and with
tools/o2r_packer/o2r_writer.py and asserts every member's bytes match."""

import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

fixture, packer_dir = sys.argv[1], sys.argv[2]
sys.path.insert(0, packer_dir)
from o2r_writer import Endianness, O2rWriter  # noqa: E402

ramp = bytes((i // 3) & 0xFF for i in range(5000))
noise = bytearray()
state = 12345
for _ in range(300):
    state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
    noise.append(state >> 24)
noise = bytes(noise)

with tempfile.TemporaryDirectory() as tmp:
    cpp_path = Path(tmp) / "cpp.o2r"
    py_path = Path(tmp) / "py.o2r"
    subprocess.run([fixture, str(cpp_path)], check=True)

    with O2rWriter(py_path) as writer:
        writer.add_resource("blob/ramp", "OBLB", 0, 0x1122334455667788, ramp)
        writer.add_resource("blob/noise", "OBLB", 2, 5, noise)
        writer.add_resource("tex/noise", "OTEX", 1, 99, noise)
        writer.add_resource("blob/little", "OBLB", 3, 77, ramp, byte_order=Endianness.LITTLE, is_custom=True)
        writer.add_resource("blob/empty", "OBLB", 0, 0, b"")
        writer.add_raw_file("raw/file", noise)

    with zipfile.ZipFile(cpp_path) as cpp, zipfile.ZipFile(py_path) as py:
        assert cpp.testzip() is None, "C++ archive has a bad CRC"
        assert sorted(cpp.namelist()) == sorted(py.namelist()), (cpp.namelist(), py.namelist())
        for name in py.namelist():
            assert cpp.read(name) == py.read(name), f"member differs: {name}"
print("ok")
