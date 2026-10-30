"""The monitor screens' command lists (src/game/propobj.c), with the MON* macros of chrai.h expanded, and the
attract-mode demo table (src/game/ramromreplay.c)."""

from __future__ import annotations

import re
from typing import Dict, List, Optional, Tuple

from .common import Decomp, Output, TableError, balanced, banner, cstr, footer, strip_comments

_LIST_RE = re.compile(r"\bu32\s+(mon\w+)\s*\[\s*\]\s*=\s*\{")
_DEFINE_RE = re.compile(r"^[ \t]*#define\s+(\w+)(\([^)]*\))?[ \t]+(.+?)[ \t]*$", re.M)
_CASE_RE = re.compile(r"case\s+(\d+)\s*:\s*image\s*=\s*&(\w+)\s*;")
_SET_IMAGE_RE = re.compile(r"void\s+monitorSetImageByNum\s*\(")
_DEFAULT_IMAGE_RE = re.compile(r"s32\s*\*\s*image\s*=\s*&(\w+)\s*;")
_ENUM_RE = re.compile(r"enum\s+MONIMGIDS\s*\{(.*?)\}\s*;", re.S)


def parse_macros(header: str) -> Dict[str, Tuple[Optional[List[str]], str]]:
    """The MON*, COLOR_* and *_CHANCE macros of chrai.h: name -> (parameters for a function-like macro or None,
    body)."""
    macros: Dict[str, Tuple[Optional[List[str]], str]] = {}
    for match in _DEFINE_RE.finditer(strip_comments(header)):
        name, params, body = match.groups()
        if not (name.startswith("MON") or name.startswith("COLOR_") or name.endswith("_CHANCE")):
            continue
        parameters = [p.strip() for p in params[1:-1].split(",") if p.strip()] if params is not None else None
        macros[name] = (parameters, body)
    return macros


def parse_image_ids(textures: str) -> Dict[str, int]:
    match = _ENUM_RE.search(strip_comments(textures))
    if match is None:
        raise TableError("no MONIMGIDS enumeration")
    names = [name.strip() for name in match.group(1).split(",") if name.strip()]
    return {name: index for index, name in enumerate(names)}


def _split_arguments(text: str) -> List[str]:
    arguments: List[str] = []
    depth = 0
    current = ""
    for char in text:
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        if char == "," and depth == 0:
            arguments.append(current.strip())
            current = ""
        else:
            current += char
    if current.strip():
        arguments.append(current.strip())
    return arguments


def _expand(text: str, macros) -> str:
    pattern = re.compile(r"\b(" + "|".join(sorted(macros, key=len, reverse=True)) + r")\b")
    for _ in range(100000):
        match = pattern.search(text)
        if match is None:
            return text
        parameters, body = macros[match.group(1)]
        end = match.end()
        replacement = body
        if parameters is not None:
            open_at = text.index("(", end)
            depth = 0
            close = open_at
            for index in range(open_at, len(text)):
                if text[index] == "(":
                    depth += 1
                elif text[index] == ")":
                    depth -= 1
                    if depth == 0:
                        close = index
                        break
            arguments = _split_arguments(text[open_at + 1 : close])
            for parameter, argument in zip(parameters, arguments):
                replacement = re.sub(r"\b" + re.escape(parameter) + r"\b", argument, replacement)
            end = close + 1
        text = text[: match.start()] + replacement + text[end:]
    raise TableError("macro expansion does not end")


def _word(token: str, lists, images) -> Tuple[Optional[str], int]:
    """(list name, 0) for a reference to another list, (None, value) for a number."""
    token = token.strip().strip("()").strip()
    if token in lists:
        return token, 0
    if token in images:
        return None, images[token]
    try:
        value = int(token, 0)
    except ValueError:
        raise TableError(f"unknown token {token!r}") from None
    return None, value & 0xFFFFFFFF


def generate_monitor_lists(decomp: Decomp) -> Output:
    source = strip_comments(decomp.read("src/game/propobj.c"))
    macros = parse_macros(decomp.read("src/game/chrai.h"))
    images = parse_image_ids(decomp.read("assets/oddtextures.h"))

    bodies: Dict[str, str] = {}
    for match in _LIST_RE.finditer(source):
        body, _ = balanced(source, match.end() - 1)
        bodies[match.group(1)] = body
    if not bodies:
        raise TableError("propobj.c holds no monitor lists")
    lists = {name: index for index, name in enumerate(bodies)}

    start = _SET_IMAGE_RE.search(source)
    if start is None:
        raise TableError("propobj.c has no monitorSetImageByNum")
    function, _ = balanced(source, source.index("{", start.end()))
    default = _DEFAULT_IMAGE_RE.search(function)
    if default is None or default.group(1) not in lists:
        raise TableError("monitorSetImageByNum has no default list")
    cases = _CASE_RE.findall(function)
    for number, name in cases:
        if name not in lists:
            raise TableError(f"image {number} names the unknown list {name}")

    out = banner("the GoldenEye decomp's src/game/propobj.c, src/game/chrai.h and assets/oddtextures.h")
    out += ['#include "extract/DecompTables.h"', "", "namespace Frigate::Extract::Detail {", ""]
    out.append("// Each monitor command list as the words the game runs, with the MON* macros expanded. Ref(name) is a")
    out.append("// reference to another list, Num(n) a number (an image of the monitor image table is its index).")
    out.append("")
    for name, body in bodies.items():
        out.append(f"static const MonitorWord k_{name}[] = {{")
        for command in _split_arguments(re.sub(r"\s+", " ", body)):
            words = [
                _word(token, lists, images) for token in _expand(command, macros).split(",") if token.strip()
            ]
            cells = [f"Ref({cstr(w[0])})" if w[0] is not None else f"Num({w[1]})" for w in words]
            out.append(f"    {', '.join(cells)}, // {command}")
        out.append("};")
    out.append("")
    out.append("const MonitorList kMonitorLists[] = {")
    for name in bodies:
        out.append(f"    {{{cstr(name)}, {{k_{name}, sizeof(k_{name}) / sizeof(k_{name}[0])}}}},")
    out += ["};", "const size_t kMonitorListCount = sizeof(kMonitorLists) / sizeof(kMonitorLists[0]);", ""]
    out.append("// monitorSetImageByNum: the list a monitor shows when its image number is not one of the cases.")
    out.append(f"const char* const kMonitorDefaultList = {cstr(default.group(1))};")
    out.append("")
    out.append("// {image number, list}")
    out.append("const MonitorImage kMonitorImages[] = {")
    for number, name in cases:
        out.append(f"    {{{number}, {cstr(name)}}},")
    out += ["};", "const size_t kMonitorImageCount = sizeof(kMonitorImages) / sizeof(kMonitorImages[0]);", ""]
    out += ["}  // namespace Frigate::Extract::Detail"] + footer()
    return Output("src/extract/MonitorListData.cpp", "\n".join(out))


_TABLE_RE = re.compile(r"struct\s+ramrom_struct\s+ramrom_table\s*\[\]\s*=\s*\{(.*?)\};", re.S)
_ENTRY_RE = re.compile(r"\{\s*&ramrom_(?P<name>\w+)\s*,\s*(?P<locked>\d+)\s*\}")


def generate_ramrom(decomp: Decomp) -> Output:
    match = _TABLE_RE.search(decomp.read("src/game/ramromreplay.c"))
    if match is None:
        raise TableError("no `ramrom_table` in ramromreplay.c")
    entries = [(m.group("name"), int(m.group("locked"))) for m in _ENTRY_RE.finditer(match.group(1))]
    if not entries:
        raise TableError("`ramrom_table` has no entries")
    out = banner("the GoldenEye decomp's src/game/ramromreplay.c")
    out += ['#include "extract/DecompTables.h"', "", "namespace Frigate::Extract::Detail {", ""]
    out.append("// ramrom_table: {demo name (the file is ramrom_<name>.bin), unlock threshold}, in the order the attract mode")
    out.append("// picks from.")
    out.append("const RamromDemo kRamromDemos[] = {")
    for name, locked in entries:
        out.append(f"    {{{cstr(name)}, {locked}}},")
    out += ["};", "const size_t kRamromDemoCount = sizeof(kRamromDemos) / sizeof(kRamromDemos[0]);", ""]
    out += ["}  // namespace Frigate::Extract::Detail"] + footer()
    return Output("src/extract/RamromData.cpp", "\n".join(out))
