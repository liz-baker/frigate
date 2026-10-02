#!/usr/bin/env python3
"""End-to-end check of `frigate --play-sound`: a synthetic tone bank is packed
into an archive, played through the real audio path into SDL's disk audio
driver, and the captured PCM is measured. No ROM or sound device needed.

Usage: sound_test_e2e.py <frigate executable> <tools/o2r_packer directory>
"""

import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

FREQUENCY = 1000.0
OUTPUT_RATE = 44100


def run(frigate, archive, cwd, *args):
    env = dict(os.environ, SDL_AUDIODRIVER="disk")
    return subprocess.run([frigate, "--play-sound", *args, str(archive)], cwd=cwd, env=env,
                          capture_output=True, text=True, timeout=120)


def read_captured(path):
    data = path.read_bytes()
    frames = len(data) // 4
    return [struct.unpack_from("<hh", data, i * 4) for i in range(frames)]


def estimate_frequency(samples):
    """Zero crossings per second over the loud middle of the capture."""
    loud = [i for i, (left, _) in enumerate(samples) if abs(left) > 4000]
    start, end = loud[0] + 200, loud[-1] - 200
    crossings = sum(1 for i in range(start, end) if samples[i][0] < 0 <= samples[i + 1][0])
    return crossings * OUTPUT_RATE / (end - start)


def main():
    frigate, tools = sys.argv[1], sys.argv[2]
    sys.path.insert(0, tools)
    from audio_fixture import write_tone_archive

    failures = []

    def check(condition, what):
        if not condition:
            failures.append(what)
            print(f"FAIL: {what}", file=sys.stderr)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        archive = tmp / "tone.o2r"
        write_tone_archive(archive, FREQUENCY, 1.0)

        result = run(frigate, archive, tmp, "tone")
        check(result.returncode == 0, f"playing a sound by name succeeds (exit {result.returncode})")
        if result.returncode != 0:
            print(result.stdout[-2000:], result.stderr[-2000:], file=sys.stderr)
        captured = tmp / "sdlaudio.raw"
        check(captured.exists(), "audio reached the output")
        if captured.exists() and captured.stat().st_size > 0:
            samples = read_captured(captured)
            seconds = len(samples) / OUTPUT_RATE
            peak_left = max(abs(left) for left, _ in samples)
            peak_right = max(abs(right) for _, right in samples)
            check(0.1 < seconds < 1.0, f"the sound lasts about its envelope, not the wave ({seconds:.2f} s)")
            check(peak_left > 5000 and peak_right > 5000, f"the tone is audible on both channels ({peak_left}, {peak_right})")
            frequency = estimate_frequency(samples)
            check(abs(frequency - FREQUENCY) < FREQUENCY * 0.02, f"the tone's pitch is {FREQUENCY} Hz ({frequency:.0f})")
            check(samples[-1] == (0, 0), "the output ends in silence")

        for query in ("1", "audio/sfx/tone_sfx", "0x1"):
            captured.unlink(missing_ok=True)
            result = run(frigate, archive, tmp, query)
            check(result.returncode == 0 and captured.exists(), f"a sound is found by '{query}'")

        result = run(frigate, archive, tmp, "no_such_sound")
        check(result.returncode != 0, "an unknown sound is an error")
        result = run(frigate, archive, tmp, "2")
        check(result.returncode != 0, "a number past the bank is an error")

    if failures:
        return 1
    print("sound_test_e2e passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
