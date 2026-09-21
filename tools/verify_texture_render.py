#!/usr/bin/env python3
"""Draw a texture through the viewer's G_SETTEX expansion and compare the
on-screen pixels with the source image decoded straight from the archive.

Needs a built viewer, `xvfb-run` and ImageMagick's `import`. Only non-paletted
formats are decoded here.

    tools/verify_texture_render.py archives/goldeneye_textures.o2r NAME [NAME...]
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time
import zipfile

from PIL import Image

HEADER_SIZE = 64
# Fast::TextureType values that are non-paletted, by name.
TYPES = {2: "RGBA16", 5: "I4", 6: "I8", 7: "IA4", 8: "IA8", 9: "IA16"}
MAGENTA = (255, 0, 255)


def decode(archive, name):
    payload = zipfile.ZipFile(archive).read(f"textures/{name}")
    kind, width, height = struct.unpack(">III", payload[HEADER_SIZE : HEADER_SIZE + 12])
    if kind not in TYPES:
        raise SystemExit(f"{name}: type {kind} is not a non-paletted format this check decodes")
    data = payload[HEADER_SIZE + 28 :]
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            if kind == 2:
                v = (data[(y * width + x) * 2] << 8) | data[(y * width + x) * 2 + 1]
                row.append(tuple(c * 255 // 31 for c in ((v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31)))
                continue
            if kind in (5, 7):
                byte = data[y * ((width + 1) // 2) + x // 2]
                nib = byte >> 4 if x % 2 == 0 else byte & 15
                i = nib * 17 if kind == 5 else (nib >> 1) * 0x24
            elif kind == 6:
                i = data[y * width + x]
            elif kind == 8:
                i = (data[y * width + x] >> 4) * 17
            else:
                i = data[(y * width + x) * 2]
            row.append((i, i, i))
        rows.append(row)
    return TYPES[kind], width, height, rows


def screenshot(viewer, archives, name, out):
    display = subprocess.Popen(["Xvfb", ":97", "-screen", "0", "1280x720x24"], stderr=subprocess.DEVNULL)
    env = dict(os.environ, DISPLAY=":97")
    try:
        time.sleep(1)
        app = subprocess.Popen([viewer, *archives, "--texture", f"textures/{name}"], env=env,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(8)
        subprocess.run(["import", "-window", "root", out], env=env, check=True)
        app.kill()
    finally:
        display.kill()


def compare(shot, width, height, rows, tolerance=2):
    img = Image.open(shot).convert("RGB")
    magenta = [(x, y) for y in range(img.height) for x in range(img.width) if img.getpixel((x, y)) == MAGENTA]
    if not magenta:
        return None
    left = min(x for x, _ in magenta)
    top = min(y for _, y in magenta)
    scale = (max(x for x, _ in magenta) + 1 - left) / 320
    bad = 0
    for y in range(height):
        for x in range(width):
            got = img.getpixel((int(left + (16 + x + 0.5) * scale), int(top + (16 + y + 0.5) * scale)))
            if any(abs(a - b) > tolerance for a, b in zip(got, rows[y][x])):
                bad += 1
    return bad


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("archive")
    parser.add_argument("names", nargs="+")
    parser.add_argument("--viewer", default="build/frigate")
    parser.add_argument("--extra-archive", nargs="*", default=[])
    args = parser.parse_args()

    failed = False
    with tempfile.TemporaryDirectory() as tmp:
        for name in args.names:
            kind, width, height, rows = decode(args.archive, name)
            shot = os.path.join(tmp, f"{name}.png")
            screenshot(args.viewer, [args.archive, *args.extra_archive], name, shot)
            bad = compare(shot, width, height, rows)
            if bad is None:
                print(f"{name} ({kind} {width}x{height}): nothing drawn")
                failed = True
                continue
            print(f"{name} ({kind} {width}x{height}): {bad}/{width * height} texels differ")
            failed |= bad != 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
