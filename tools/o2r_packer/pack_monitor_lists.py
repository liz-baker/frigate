#!/usr/bin/env python3
"""Packs the monitor screens' animation command lists.

The GoldenEye decomp's propobj.c keeps what each monitor shows as `u32`
arrays of commands (scroll, zoom, hold, jump on chance, ...) written with the
`MON*` macros of chrai.h, and `monitorSetImageByNum` maps the numbers the
stage setup uses to those arrays. A command names another list, an image of
the monitor image table or a colour as a number or a constant; this expands
the macros and writes the words out, a reference to a list as `@<name>`.
Layout: docs/formats/goldeneye-monitor-lists.md.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union

from o2r_writer import O2rWriter

RESOURCE_PATH = "monitor/lists"

_LIST_RE = re.compile(r"\bu32\s+(mon\w+)\s*\[\s*\]\s*=\s*\{")
_DEFINE_RE = re.compile(r"^[ \t]*#define\s+(\w+)(\([^)]*\))?[ \t]+(.+?)[ \t]*$", re.M)
_CASE_RE = re.compile(r"case\s+(\d+)\s*:\s*image\s*=\s*&(\w+)\s*;")
_SET_IMAGE_RE = re.compile(r"void\s+monitorSetImageByNum\s*\(")
_DEFAULT_IMAGE_RE = re.compile(r"s32\s*\*\s*image\s*=\s*&(\w+)\s*;")
_ENUM_RE = re.compile(r"enum\s+MONIMGIDS\s*\{(.*?)\}\s*;", re.S)


class MonitorListsError(ValueError):
    """Raised when the decomp does not hold the lists."""


def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def _balanced(text: str, open_at: int) -> Tuple[str, int]:
    depth = 0
    for index in range(open_at, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1 : index], index + 1
    raise MonitorListsError("unbalanced braces")


def parse_macros(header: str) -> Dict[str, Tuple[Optional[List[str]], str]]:
    """The MON*, COLOR_* and *_CHANCE macros of chrai.h: name -> (parameters
    for a function-like macro or None, body)."""
    macros: Dict[str, Tuple[Optional[List[str]], str]] = {}
    for match in _DEFINE_RE.finditer(_strip_comments(header)):
        name, params, body = match.groups()
        if not (name.startswith("MON") or name.startswith("COLOR_") or name.endswith("_CHANCE")):
            continue
        parameters = [p.strip() for p in params[1:-1].split(",") if p.strip()] if params is not None else None
        macros[name] = (parameters, body)
    return macros


def parse_image_ids(textures: str) -> Dict[str, int]:
    """The monitor image table's enumeration (MONIMGIDS)."""
    match = _ENUM_RE.search(_strip_comments(textures))
    if match is None:
        raise MonitorListsError("no MONIMGIDS enumeration")
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


def _expand(text: str, macros: Dict[str, Tuple[Optional[List[str]], str]]) -> str:
    """Expands every macro in `text`."""
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
    raise MonitorListsError("macro expansion does not end")


def _word(token: str, lists: Dict[str, int], images: Dict[str, int]) -> str:
    token = token.strip().strip("()").strip()
    if token in lists:
        return "@" + token
    if token in images:
        return str(images[token])
    try:
        value = int(token, 0)
    except ValueError:
        raise MonitorListsError(f"unknown token {token!r}") from None
    return str(value & 0xFFFFFFFF)


def build_monitor_lists(propobj: str, header: str, textures: str) -> bytes:
    """The resource's bytes. Layout: docs/formats/goldeneye-monitor-lists.md."""
    source = _strip_comments(propobj)
    macros = parse_macros(header)
    images = parse_image_ids(textures)

    bodies: Dict[str, str] = {}
    for match in _LIST_RE.finditer(source):
        body, _ = _balanced(source, match.end() - 1)
        bodies[match.group(1)] = body
    if not bodies:
        raise MonitorListsError("propobj.c holds no monitor lists")
    lists = {name: index for index, name in enumerate(bodies)}

    lines: List[str] = []
    for name, body in bodies.items():
        words = [
            _word(token, lists, images)
            for token in _expand(body, macros).replace("\n", " ").split(",")
            if token.strip()
        ]
        lines.append("\t".join(("list", name, *words)))

    start = _SET_IMAGE_RE.search(source)
    if start is None:
        raise MonitorListsError("propobj.c has no monitorSetImageByNum")
    function, _ = _balanced(source, source.index("{", start.end()))
    default = _DEFAULT_IMAGE_RE.search(function)
    if default is None or default.group(1) not in lists:
        raise MonitorListsError("monitorSetImageByNum has no default list")
    lines.append("\t".join(("default", default.group(1))))
    for number, name in _CASE_RE.findall(function):
        if name not in lists:
            raise MonitorListsError(f"image {number} names the unknown list {name}")
        lines.append("\t".join(("image", number, name)))
    return ("\n".join(lines) + "\n").encode("ascii")


def pack_monitor_lists(decomp_dir: Union[str, Path], writer: O2rWriter) -> str:
    decomp = Path(decomp_dir)
    propobj = (decomp / "src" / "game" / "propobj.c").read_text()
    header = (decomp / "src" / "game" / "chrai.h").read_text()
    textures = (decomp / "assets" / "oddtextures.h").read_text()
    writer.add_resource(
        RESOURCE_PATH, "OBLB", version=0, resource_id=0, payload=build_monitor_lists(propobj, header, textures)
    )
    return RESOURCE_PATH
