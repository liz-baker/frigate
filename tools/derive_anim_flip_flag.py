#!/usr/bin/env python3
"""Derives what the title stage's texture pool holds at each stage's
animFlipFlag address. Method and layout: docs/determinism-anim-flip-flag.md.

Usage: derive_anim_flip_flag.py <GoldenEye 007 (USA).z64> <imagelist.u.csv>
"""
import csv
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "o2r_packer"))
import ge_texture  # noqa: E402
from ge_texture import TexFormat  # noqa: E402

GLOBAL_IMAGE_TABLE = 0x29D160
# Display lists texReset walks, as offsets into the global image table.
DISPLAY_LISTS = [0x000, 0x078, 0x120, 0x1C8, 0x270, 0x318, 0x3C0, 0x468,
                 0x510, 0x5B8, 0x660, 0x708, 0x7B0, 0x858, 0x900, 0x9A8, 0xA50]
GENERIC_IMAGE = 0xAC8
SMOKE_IMAGES = GENERIC_IMAGE + 12 + 20 * 12  # after the impact images
SCATTERED_EXPLOSIONS = SMOKE_IMAGES + 6 * 12

# Title layout (-ma1 -mgfx80 -mvtx20 -mt646): MA bank, player struct, two gfx
# buffers, two vertex buffers, then the texture pool.
TITLE_TEXTURE_POOL = 0x400 + 0x2A80 + 2 * 0x14000 + 2 * 0x5000
# animFlipFlag offsets from the stage pool start: -maNNN * 1024 + 0x5BC.
FLAG_OFFSETS = {"Dam": 0x451BC, "Facility": 0x285BC, "Runway": 0x4B5BC,
                "Bunker1/Silo": 0x25DBC, "Frigate": 0x389BC, "Train": 0x325BC}


def stride_bytes(fmt, w, h):
    if fmt in (TexFormat.RGBA32, TexFormat.RGB24):
        return ((w + 3) & 0xFFC) * h * 4
    if fmt in (TexFormat.RGBA16, TexFormat.IA16, TexFormat.RGB15):
        return ((w + 3) & 0xFFC) * h * 2
    if fmt in (TexFormat.IA8, TexFormat.I8):
        return ((w + 7) & 0xFF8) * h
    return (((w + 15) & 0xFF0) >> 1) * h


def main():
    rom = open(sys.argv[1], "rb").read()
    imgs = [(int(r[0]), int(r[1])) for r in csv.reader(open(sys.argv[2]))]

    def entry_id(off):
        return struct.unpack_from(">I", rom, GLOBAL_IMAGE_TABLE + off)[0] & 0xFFFF

    order = []
    for dl in DISPLAY_LISTS:
        p = GLOBAL_IMAGE_TABLE + dl
        while rom[p] != 0xB8:
            if rom[p] == 0xFD and rom[p + 4] == 0xAB and rom[p + 5] == 0xCD:
                order.append(struct.unpack(">I", rom[p + 4:p + 8])[0] & 0xFFFF)
            p += 8
    order.append(entry_id(GENERIC_IMAGE))
    order += [entry_id(SMOKE_IMAGES + 12 * i) for i in range(6)]
    order += [entry_id(SCATTERED_EXPLOSIONS + 12 * i) for i in range(5)]

    pos, rows, seen = 0, [], set()
    for t in order:
        if t in seen:
            continue
        seen.add(t)
        data = rom[imgs[t][0]:imgs[t][0] + imgs[t][1]]
        lod, zlib, explicit = data[0] & 0x3F, (data[0] >> 6) & 1, data[0] >> 7
        if zlib or explicit:
            raise SystemExit(f"texture {t}: zlib/explicit-LOD layout not modelled")
        tex = ge_texture.decode_texture(data)
        pos += 8  # the pool's per-texture header
        size = (stride_bytes(tex.format, tex.width, tex.height) + 7) & ~7
        w, h = tex.width, tex.height
        for _ in range(1, lod):  # shrunk LODs, each unaligned
            w, h = (w + 1) >> 1, (h + 1) >> 1
            size += stride_bytes(tex.format, w, h)
        rows.append((t, pos, size, tex))
        pos += size
    print(f"texReset prefix: {len(rows)} textures, {pos:#x} bytes")

    for name, flag in FLAG_OFFSETS.items():
        rel = flag - TITLE_TEXTURE_POOL
        hit = next((r for r in rows if r[1] <= rel < r[1] + r[2]), None)
        if hit is None:
            print(f"{name}: pool offset {rel:#x} is outside the prefix (not derivable)")
            continue
        t, start, size, tex = hit
        stride = stride_bytes(tex.format, tex.width, 1)
        row, col = divmod(rel - start, stride)
        if col >= tex.width or tex.format not in (TexFormat.IA8, TexFormat.I8):
            print(f"{name}: texture {t} {tex.format.name} offset {rel - start:#x} (padding or other format)")
            continue
        value = tex.pixel_data[row * tex.width + col]
        print(f"{name}: pool {rel:#x} -> texture {t} {tex.format.name} {tex.width}x{tex.height} "
              f"row {row} col {col} = {value:#04x} (animFlipFlag {'true' if value else 'false'})")


if __name__ == "__main__":
    main()
