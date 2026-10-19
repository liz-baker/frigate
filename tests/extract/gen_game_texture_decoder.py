#!/usr/bin/env python3
"""Builds the game-code texture oracle's source from the GoldenEye decomp at build time.

usage: gen_game_texture_decoder.py <decomp-root> <output.inc>

The output holds the decomp's own non-paletted texture decompression (src/game/image.c, src/game/image_bank.c)
lifted out function by function, with a handful of mechanical host adaptations listed in PATCHES. It is a build
artifact: the decomp is unlicensed upstream, so the generated file is never committed. GameTextureOracle.c wraps it
and TextureOracleTest.cpp compares it with frigate_extract's decoder (docs/extract-verification.md)."""

import re
import sys
from pathlib import Path

IMAGE_FUNCTIONS = [
    "texInflateHuffman",
    "texInflateRle",
    "texBuildLookup",
    "texGetBitSize",
    "texReadAlphaBits",
    "texReadUncompressed",
    "texChannelsToPixels",
    "texInflateLookup",
    "texInflateLookupFromBuffer",
    "texSwapAltRowBytes",
    "texBlur",
    "texShrinkNonPaletted",
    "texInflateNonZlib",
]
BANK_FUNCTIONS = ["texSetBitstring", "texReadBits"]
TABLES = ["g_TexFormatNumChannels", "g_TexFormatHas1BitAlpha", "g_TexFormatChannelSizes", "g_TexFormatBitsPerPixel"]

# (function, old, new, occurrences): each must match exactly that many times.
PATCHES = [
    # Host adaptations: the game runs on a big-endian 32-bit CPU, and these are the places its code depends on that.
    # 64-bit pointers do not fit a u32.
    ("texReadUncompressed", "(u32)dst", "(uintptr_t)dst", 3),
    # The table entry's low byte is the second byte of a big-endian halfword.
    ("texInflateLookup", "lookup[(texReadBits(bitspercolour) * 2) + 1]",
     "lookup[(texReadBits(bitspercolour) * 2) + TEX_LOW_BYTE]", 1),
    # The bitstream is a host pointer here, not a 32-bit address.
    ("texSetBitstring", "s32 pos", "u8 *pos", 1),
    # Stack buffers the game leaves uninitialised, which makes some outputs depend on stale memory. Zero them so the
    # oracle is deterministic. The scratch buffer is also too small for a 1-bit alpha plane at 3 * width * height on
    # the larger IA4 textures, where the game overruns into whatever follows it on the stack; give it room.
    ("texInflateNonZlib", "u8 scratch[0x2000];", "static u8 scratch[0x10000]; memset(scratch, 0, sizeof(scratch));", 1),
    ("texInflateNonZlib", "u8 lookup[0x1000];", "u8 lookup[0x1000] = { 0 };", 1),
]


def function_text(source: str, name: str) -> str:
    lines = source.splitlines()
    pattern = re.compile(r"^[A-Za-z_][\w \*]*?\b" + name + r"\(")
    for start, line in enumerate(lines):
        if pattern.match(line):
            for end in range(start, len(lines)):
                if lines[end] == "}":
                    return "\n".join(lines[start : end + 1])
    raise SystemExit(f"function {name} not found")


def table_text(source: str, name: str) -> str:
    match = re.search(r"^s32 " + name + r"\[\] =\s*\{.*?\};", source, re.S | re.M)
    if not match:
        raise SystemExit(f"table {name} not found")
    return match.group(0)


def main() -> None:
    root = Path(sys.argv[1])
    image = (root / "src/game/image.c").read_text()
    bank = (root / "src/game/image_bank.c").read_text()
    header = (root / "src/game/image.h").read_text()

    out = re.findall(r"^#define TEX(?:FORMAT|COMPMETHOD)_\w+\s+\w+", header, re.M)
    out += [table_text(image, name) for name in TABLES]
    functions = {}
    for name in BANK_FUNCTIONS:
        functions[name] = function_text(bank, name)
    for name in IMAGE_FUNCTIONS:
        functions[name] = function_text(image, name)
    for function, old, new, count in PATCHES:
        if functions[function].count(old) != count:
            raise SystemExit(f"patch for {function} does not apply: {old!r}")
        functions[function] = functions[function].replace(old, new)
    # Callees first, so the file needs no prototypes.
    out += [functions[name] for name in BANK_FUNCTIONS + IMAGE_FUNCTIONS]
    Path(sys.argv[2]).write_text("\n\n".join(out) + "\n")


main()
