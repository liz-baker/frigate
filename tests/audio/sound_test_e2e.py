#!/usr/bin/env python3
"""End-to-end check of `frigate --play-sound`: a synthetic tone bank and two
music tracks that play it are packed into an archive, played through the real
audio path into SDL's disk audio driver, and the captured PCM is measured. No
ROM or sound device needed.

Usage: sound_test_e2e.py <frigate executable> <tools/o2r_packer directory>
"""

import os
import signal
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

FREQUENCY = 1000.0
OUTPUT_RATE = 44100


def run(frigate, archive, cwd, *args):
    env = dict(os.environ, SDL_AUDIODRIVER="disk")
    return subprocess.run([frigate, "--play-sound", *args, "--archive-dir", str(archive.parent)], cwd=cwd, env=env,
                          capture_output=True, text=True, timeout=120)


def read_captured(path):
    data = path.read_bytes()
    frames = len(data) // 4
    return [struct.unpack_from("<hh", data, i * 4) for i in range(frames)]


def estimate_frequency(samples, threshold=4000):
    """Zero crossings per second over the loud middle of the capture."""
    loud = [i for i, (left, _) in enumerate(samples) if abs(left) > threshold]
    start, end = loud[0] + 200, loud[-1] - 200
    crossings = sum(1 for i in range(start, end) if samples[i][0] < 0 <= samples[i + 1][0])
    return crossings * OUTPUT_RATE / (end - start)


def bursts(samples, gap_seconds=0.15, loud=500):
    """(start, end) frame ranges of sound, split where a quiet gap is at
    least `gap_seconds` long."""
    found = []
    start = last = None
    gap = int(gap_seconds * OUTPUT_RATE)
    for i, (left, _) in enumerate(samples):
        if abs(left) > loud:
            if start is None:
                start = i
            elif i - last > gap:
                found.append((start, last))
                start = i
            last = i
    if start is not None:
        found.append((start, last))
    return found


def check_music(frigate, archive, tmp, check):
    from audio_fixture import MUSIC_KEY

    # An octave above the tone's base key plays it at twice the pitch.
    expected = FREQUENCY * 2 ** ((MUSIC_KEY - 60) / 12)
    captured = tmp / "sdlaudio.raw"

    captured.unlink(missing_ok=True)
    result = run(frigate, archive, tmp, "M_BLIP", "--play-seconds", "3")
    check(result.returncode == 0, f"playing a music track by name succeeds (exit {result.returncode})")
    if result.returncode != 0:
        print(result.stdout[-2000:], result.stderr[-2000:], file=sys.stderr)
        return
    samples = read_captured(captured)
    seconds = len(samples) / OUTPUT_RATE
    check(3.0 < seconds < 6.0, f"a track plays for the time asked, plus its tail ({seconds:.2f} s)")
    found = bursts(samples)
    check(len(found) >= 2, f"a track that ends is played again ({len(found)} plays)")
    if found:
        first = found[0]
        check(0.3 < (first[1] - first[0]) / OUTPUT_RATE < 1.0, "the note lasts about its duration")
        frequency = estimate_frequency(samples[first[0] : first[1]], 800)
        check(abs(frequency - expected) < expected * 0.02, f"the note is at pitch {expected:.0f} Hz ({frequency:.0f})")
    check(samples[-1] == (0, 0), "the output ends in silence")

    for query in ("blip", "audio/music/m_blip", "audio/music/1"):
        captured.unlink(missing_ok=True)
        result = run(frigate, archive, tmp, query, "--play-seconds", "1")
        check(result.returncode == 0 and captured.exists(), f"a track is found by '{query}'")

    # A track that never ends plays until interrupted, then stops cleanly.
    captured.unlink(missing_ok=True)
    env = dict(os.environ, SDL_AUDIODRIVER="disk")
    proc = subprocess.Popen([frigate, "--play-sound", "M_DRONE", "--archive-dir", str(archive.parent)], cwd=tmp, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    deadline = time.time() + 60
    while time.time() < deadline and not (captured.exists() and captured.stat().st_size > 44100 * 4 * 3):
        time.sleep(0.1)
    proc.send_signal(signal.SIGINT)
    try:
        proc.communicate(timeout=30)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()
        check(False, "an interrupted track stops")
        return
    check(proc.returncode == 0, f"an interrupted track exits cleanly (exit {proc.returncode})")
    samples = read_captured(captured)
    found = bursts(samples)
    check(len(found) == 1 and (found[0][1] - found[0][0]) / OUTPUT_RATE > 2.5,
          f"a looping track plays without a break ({[(a / OUTPUT_RATE, b / OUTPUT_RATE) for a, b in found]})")
    check(samples[-1] == (0, 0), "the interrupted output ends in silence")

    result = run(frigate, archive, tmp, "M_NO_SUCH_TRACK")
    check(result.returncode != 0, "an unknown track is an error")


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
        archive = tmp / "goldeneye_audio.o2r"
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

        check_music(frigate, archive, tmp, check)

    if failures:
        return 1
    print("sound_test_e2e passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
