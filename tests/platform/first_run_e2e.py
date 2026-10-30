#!/usr/bin/env python3
"""End-to-end check of the game's first-run extraction, with the real frigate executable.

An empty archive directory and the US ROM in the working directory: `frigate --play-sound` (no window, so it
runs headless) extracts the four archives, then plays a sound from the fresh audio archive. Also the paths that
must fail with a usable message: no ROM, a byte-swapped ROM, and --no-extract.

Usage: first_run_e2e.py <frigate executable>
Exits 77 (ctest SKIP_RETURN_CODE) when there is no US ROM in FRIGATE_ROMS.
"""

import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77
ROM_SIZE = 12 * 1024 * 1024
US_SHA1 = "abe01e4aeb033b6c0836819f549c791b26cfde83"
ARCHIVES = ["textures", "objects", "geometry", "audio"]

frigate = sys.argv[1]


def find_us_rom():
    for location in os.environ.get("FRIGATE_ROMS", "").split(os.pathsep):
        path = Path(location)
        candidates = sorted(path.iterdir()) if path.is_dir() else [path]
        for candidate in candidates:
            if candidate.is_file() and candidate.stat().st_size == ROM_SIZE:
                if hashlib.sha1(candidate.read_bytes()).hexdigest() == US_SHA1:
                    return candidate
    return None


def run(cwd, archives, *args):
    env = dict(os.environ, SDL_AUDIODRIVER="disk", SDL_VIDEODRIVER="dummy")
    env.pop("SHIP_HOME", None)
    return subprocess.run([frigate, "--play-sound", "M_INTRO", "--play-seconds", "0.2", "--archive-dir", str(archives), *args],
                          cwd=cwd, env=env, capture_output=True, text=True, timeout=300)


def output(proc):
    return proc.stdout + proc.stderr


def check(condition, what, proc=None):
    if not condition:
        print("FAIL:", what)
        if proc is not None:
            print(output(proc))
        sys.exit(1)


def archive_names(directory):
    return sorted(p.name for p in directory.iterdir())


rom = find_us_rom()
if rom is None:
    print("SKIP: no US ROM in FRIGATE_ROMS")
    sys.exit(SKIP)

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)

    # No ROM anywhere: exit 1, the message says what was searched, nothing is written.
    work = tmp / "empty"
    work.mkdir()
    proc = run(work, tmp / "none")
    check(proc.returncode == 1, "no ROM exits 1", proc)
    check("No US GoldenEye 007 ROM" in output(proc) and str(work) in output(proc), "no-ROM message names the search", proc)
    check(not (tmp / "none").exists() or not archive_names(tmp / "none"), "no-ROM run writes nothing", proc)

    # --no-extract skips the flow even though the set is missing.
    proc = run(work, tmp / "none", "--no-extract")
    check("first run" not in output(proc), "--no-extract does not start the flow", proc)

    # A byte-swapped dump (the .v64 byte order) is reported as such.
    data = bytearray(rom.read_bytes())
    for i in range(0, len(data), 2):
        data[i], data[i + 1] = data[i + 1], data[i]
    swapped = tmp / "swapped"
    swapped.mkdir()
    (swapped / "ge.v64").write_bytes(data)
    proc = run(swapped, tmp / "none")
    check(proc.returncode == 1 and "byte-swapped" in output(proc), "byte-swapped ROM is reported", proc)

    # The ROM in the working directory: extract, then play from the new archives.
    work = tmp / "work"
    work.mkdir()
    (work / "subdir").mkdir()
    shutil.copy(rom, work / "subdir" / "not-searched.z64")
    os.symlink(rom.resolve(), work / "GoldenEye 007 (USA).z64")
    archives = tmp / "archives"
    proc = run(work, archives)
    check(proc.returncode == 0, "extraction then playback exits 0", proc)
    check(archive_names(archives) == sorted(f"goldeneye_{name}.o2r" for name in ARCHIVES),
          "the four archives, and nothing else, were written", proc)
    check("first run" in output(proc), "the flow is logged", proc)

    # With the set complete, a second start does not touch it.
    before = {p.name: p.stat().st_mtime_ns for p in archives.iterdir()}
    proc = run(work, archives)
    check(proc.returncode == 0 and "first run" not in output(proc), "a complete set does not trigger the flow", proc)
    check(before == {p.name: p.stat().st_mtime_ns for p in archives.iterdir()}, "a complete set is left untouched", proc)

    # A damaged archive is rebuilt, and --rom names the ROM explicitly.
    damaged = archives / "goldeneye_objects.o2r"
    damaged.write_bytes(damaged.read_bytes()[:1000])
    elsewhere = tmp / "elsewhere"
    elsewhere.mkdir()
    proc = run(elsewhere, archives, "--rom", str(rom))
    check(proc.returncode == 0 and "first run" in output(proc), "a damaged archive is re-extracted", proc)
    check(damaged.stat().st_size > 1000, "the damaged archive was replaced", proc)

print("ok")
