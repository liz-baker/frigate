#!/usr/bin/env python3
"""Packs the weapon table: for each item, the ammo type, magazine size and
behaviour flags of its stats, plus the ammo types' carry limits.

The GoldenEye decomp keeps per-weapon stats as `WeaponStats` structs
(`assets/obseg/gun/gunWeaponStats.inc.c` and one file per weapon) and an item
table (`gitem_structs`) that names the stats each `ITEM_IDS` value uses. An
item flagged as having no model uses `default_weaponstats`. This resolves that
table and writes one text resource. Layout:
docs/formats/goldeneye-weapon-stats.md.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union

from o2r_writer import O2rWriter

RESOURCE_PATH = "weapon/stats"

_INCLUDE_RE = re.compile(r"#include\s*<(assets/obseg/gun/[^>]+)>")
_ROW_RE = re.compile(
    r"(?:GUNFILERECORD|SUIT_LFRECORD)\(\s*\w+\s*,\s*(?P<macro_nomodel>\w+)\s*,\s*"
    r"(?P<macro_stats>GUNSTATS\(\w+\)|NULL)"
    r"|\{\s*(?P<row_header>NULL|&\w+)\s*,\s*(?:NULL|\"\w*\"|\w+)\s*,\s*(?P<row_nomodel>\w+)\s*,\s*"
    r"(?P<row_stats>GUNSTATS\(\w+\)|NULL)"
)
_STATS_NAME_RE = re.compile(r"GUNSTATS\((\w+)\)")
_STATS_RE = re.compile(r"WeaponStats\s+(\w+)\s*=\s*\{(.*?)\}\s*;", re.S)
_DEFINE_RE = re.compile(r"#define\s+(WEAPONSTATBITFLAG_\w+)\s+(0x[0-9a-fA-F]+|\d+)\b")

# The fields of `struct WeaponStats` in initializer order (the recoil speed
# union is one initializer).
_FIELDS = [
    "MuzzleFlashExtension", "PosX", "PosY", "PosZ", "PlayX", "PlayY", "PlayZ",
    "AmmoType", "MagSize", "AutomaticFiringRate", "SingleFiringRate",
    "ObjectsShootThrough", "SoundTriggerRate", "Sound", "ptr_cartridge_struct",
    "DestructionAmount", "Inaccuracy", "Zoom", "CrosshairSpeed", "AimLockSpeed",
    "Sway", "RecoilSpeed", "RecoilBack", "RecoilUp", "BoltRecoilBack",
    "LoudnessMin", "LoudnessMax", "NoiseIncreasePerShot", "NoiseDecayLinearTime",
    "NoiseDecayScaledTime", "ForceOfImpact", "BitFlags",
]  # fmt: skip


class WeaponStatsError(ValueError):
    """Raised when the decomp's tables don't line up."""


def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def _expand_includes(decomp_dir: Path, text: str) -> str:
    return _INCLUDE_RE.sub(lambda m: _strip_comments((decomp_dir / m.group(1)).read_text()), text)


def _enum_names(bondconstants: str, name: str) -> List[str]:
    """The enumerators of `typedef enum <name> { ... } <name>;`, in order,
    each taking the next value (no explicit values are used in the two enums
    read here beyond a leading -1 for ITEM_NOTHING)."""
    match = re.search(r"typedef\s+enum\s+" + name + r"\s*\{(.*?)\}\s*" + name + r"\s*;", bondconstants, re.S)
    if match is None:
        raise WeaponStatsError(f"no `{name}` enum in bondconstants.h")
    body = _strip_comments(match.group(1))
    names = []
    for item in body.split(","):
        item = item.strip()
        if item:
            names.append(item.split("=")[0].strip())
    return names


def _split_top_level(text: str) -> List[str]:
    parts, depth, current = [], 0, ""
    for char in text:
        if char in "({":
            depth += 1
        elif char in ")}":
            depth -= 1
        if char == "," and depth == 0:
            parts.append(current.strip())
            current = ""
        else:
            current += char
    if current.strip():
        parts.append(current.strip())
    return parts


def parse_flag_values(bondconstants: str) -> Dict[str, int]:
    return {name: int(value, 0) for name, value in _DEFINE_RE.findall(bondconstants)}


def parse_stats(decomp_dir: Path, ammo_values: Dict[str, int], flags: Dict[str, int]) -> Dict[str, Tuple[int, int, int]]:
    """Every `WeaponStats` definition's (ammo type, magazine size, flags)."""
    gun_dir = decomp_dir / "assets" / "obseg" / "gun"
    text = _expand_includes(decomp_dir, _strip_comments((gun_dir / "gunWeaponStats.inc.c").read_text()))
    text = re.sub(r"^\s*#(?!\s*include).*$", "", text, flags=re.M)
    stats: Dict[str, Tuple[int, int, int]] = {}
    for name, body in _STATS_RE.findall(text):
        values = _split_top_level(body)
        if len(values) != len(_FIELDS):
            raise WeaponStatsError(f"{name} has {len(values)} initializers, expected {len(_FIELDS)}")
        fields = dict(zip(_FIELDS, values))
        ammo = fields["AmmoType"]
        ammo_type = ammo_values[ammo] if ammo in ammo_values else int(ammo, 0)
        bits = 0
        for flag in re.split(r"\|", fields["BitFlags"]):
            flag = flag.strip()
            if not flag:
                continue
            bits |= flags[flag] if flag in flags else int(flag, 0)
        stats[name] = (ammo_type, int(fields["MagSize"], 0), bits)
    return stats


def parse_item_stats(decomp_dir: Path) -> List[Tuple[Optional[str], bool]]:
    """Per item in `gitem_structs` order, the name of its stats struct
    (`<name>_stats`, None for an item flagged as having no model, which takes
    the defaults) and whether the record has a model header."""
    gun_dir = decomp_dir / "assets" / "obseg" / "gun"
    table = _expand_includes(decomp_dir, _strip_comments((gun_dir / "gunModelFileRecord.inc.c").read_text()))
    table = table[table.index("gitem_structs") :]
    rows: List[Tuple[Optional[str], bool]] = []
    for match in _ROW_RE.finditer(table):
        nomodel = match.group("macro_nomodel") or match.group("row_nomodel")
        stats = match.group("macro_stats") or match.group("row_stats")
        has_header = match.group("row_header") is None or match.group("row_header").startswith("&")
        if nomodel in ("TRUE", "1") or stats == "NULL":
            rows.append((None, has_header))
        else:
            rows.append((_STATS_NAME_RE.fullmatch(stats).group(1) + "_stats", has_header))
    return rows


def parse_ammo_limits(decomp_dir: Path) -> List[int]:
    """`ammo_related`'s maximum ammo per ammo type."""
    text = _strip_comments((decomp_dir / "src" / "game" / "gun.c").read_text())
    match = re.search(r"AmmoStats\s+ammo_related\[[^\]]*\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if match is None:
        raise WeaponStatsError("no `ammo_related` table in gun.c")
    return [int(row.split(",")[0], 0) for row in re.findall(r"\{([^{}]*)\}", match.group(1))]


def build_weapon_stats(decomp_dir: Union[str, Path]) -> bytes:
    """The resource's bytes: one `item` line per ITEM_IDS value,
    `item\\t<index>\\t<name>\\t<hasHeader>\\t<ammoType>\\t<magSize>\\t<flags>`, then one
    `ammo` line per ammo type, `ammo\\t<index>\\t<name>\\t<maxAmmo>` (a type the
    enum does not name has `-` for its name)."""
    decomp_dir = Path(decomp_dir)
    bondconstants = (decomp_dir / "src" / "bondconstants.h").read_text()
    ammo_names = _enum_names(bondconstants, "AMMOTYPE")
    ammo_values = {name: index for index, name in enumerate(ammo_names)}
    item_names = [n for n in _enum_names(bondconstants, "ITEM_IDS") if n not in ("ITEM_NOTHING", "ITEM_IDS_MAX")]
    stats = parse_stats(decomp_dir, ammo_values, parse_flag_values(bondconstants))
    if "default_weaponstats" not in stats:
        raise WeaponStatsError("no `default_weaponstats`")
    rows = parse_item_stats(decomp_dir)
    if len(rows) != len(item_names):
        raise WeaponStatsError(f"ITEM_IDS names {len(item_names)} items but the table has {len(rows)} rows")
    lines = []
    for index, (name, (stats_name, has_header)) in enumerate(zip(item_names, rows)):
        key = stats_name if stats_name is not None else "default_weaponstats"
        if key not in stats:
            raise WeaponStatsError(f"{name} names stats `{key}`, which are not defined")
        ammo, magazine, bits = stats[key]
        lines.append(f"item\t{index}\t{name}\t{int(has_header)}\t{ammo}\t{magazine}\t{bits:#x}")
    limits = parse_ammo_limits(decomp_dir)
    for index, limit in enumerate(limits):
        name = ammo_names[index] if index < len(ammo_names) else "-"
        lines.append(f"ammo\t{index}\t{name}\t{limit}")
    return ("\n".join(lines) + "\n").encode("ascii")


def pack_weapon_stats(decomp_dir: Union[str, Path], writer: O2rWriter) -> str:
    writer.add_resource(RESOURCE_PATH, "OBLB", version=0, resource_id=0, payload=build_weapon_stats(decomp_dir))
    return RESOURCE_PATH
