#!/usr/bin/env python3
"""Packs the weapon table: for each item, the ammo type, magazine size,
behaviour flags, first-person placement and firing stats of its stats and the
model it holds, plus the ammo types' carry limits.

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
from typing import Callable, Dict, List, NamedTuple, Optional, Tuple, Union

from ge_model import GeModelError, read_model_header_counts
from o2r_writer import O2rWriter

RESOURCE_PATH = "weapon/stats"

_INCLUDE_RE = re.compile(r"#include\s*<(assets/obseg/gun/[^>]+)>")
_ROW_RE = re.compile(
    r"(?P<macro>GUNFILERECORD|SUIT_LFRECORD)\(\s*(?P<macro_name>\w+)\s*,\s*(?P<macro_nomodel>\w+)\s*,\s*"
    r"(?P<macro_stats>GUNSTATS\(\w+\)|NULL)"
    r"|\{\s*(?P<row_header>NULL|&\w+)\s*,\s*(?:NULL|\"\w*\"|\w+)\s*,\s*(?P<row_nomodel>\w+)\s*,\s*"
    r"(?P<row_stats>GUNSTATS\(\w+\)|NULL)"
)
_STATS_NAME_RE = re.compile(r"GUNSTATS\((\w+)\)")
_STATS_RE = re.compile(r"WeaponStats\s+(\w+)\s*=\s*\{(.*?)\}\s*;", re.S)
_DEFINE_RE = re.compile(r"#define\s+(WEAPONSTATBITFLAG_\w+)\s+(0x[0-9a-fA-F]+|\d+)\b")
# The stat tables' SCALEVAL and RATE constants. The file defines each once per
# release, the original release's first.
_SCALE_DEFINE_RE = re.compile(r"^#define\s+((?:SCALEVAL|RATE_)\w*)\s+(0[xX][0-9a-fA-F]+|[0-9.]+)f?\s*$", re.M)

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

# The stat fields the level code places the held model with, in the order they
# follow an item's flags on its `item` line.
_PLACEMENT_FIELDS = [
    "MuzzleFlashExtension", "PosX", "PosY", "PosZ", "PlayX", "PlayY", "PlayZ",
    "CrosshairSpeed", "AimLockSpeed", "Sway", "BoltRecoilBack",
]  # fmt: skip

# The stat fields the gun's firing states read, in the order they follow the
# placement fields on an `item` line. The cartridge and recoil speed take more
# than one column (see `_firing_columns`).
_FIRING_FIELDS = [
    "AutomaticFiringRate", "SingleFiringRate", "RecoilSpeed", "RecoilBack", "RecoilUp",
    "ptr_cartridge_struct", "LoudnessMin", "LoudnessMax", "NoiseIncreasePerShot",
    "NoiseDecayLinearTime", "NoiseDecayScaledTime", "Zoom", "Inaccuracy", "ObjectsShootThrough",
    "DestructionAmount",
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
    each taking the next value. An enumerator defined as an earlier enumerator
    (`AMMOTYPE_GLOBAL_MAX = AMMO_GGUN`) is an alias and takes no value of its
    own; the leading -1 of ITEM_NOTHING is the only other explicit value."""
    match = re.search(r"typedef\s+enum\s+" + name + r"\s*\{(.*?)\}\s*" + name + r"\s*;", bondconstants, re.S)
    if match is None:
        raise WeaponStatsError(f"no `{name}` enum in bondconstants.h")
    body = _strip_comments(match.group(1))
    names = []
    for item in body.split(","):
        item = item.strip()
        if item:
            name_part, _, value = item.partition("=")
            if value.strip() in names:
                continue
            names.append(name_part.strip())
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


class Stats(NamedTuple):
    """One `WeaponStats` definition's fields the table carries."""

    ammo_type: int
    magazine: int
    flags: int
    placement: Tuple[float, ...]  # _PLACEMENT_FIELDS order
    firing: Tuple[str, ...]  # the columns after the placement, as written
    force_of_impact: float = 0.0


class ItemRow(NamedTuple):
    """One `gitem_structs` row."""

    stats_name: Optional[str]  # `<name>_stats`; None for an item flagged as having no model
    has_header: bool
    model: Optional[str]  # the model's resource base path, None without a model file


def _number(token: str, scales: Dict[str, float]) -> float:
    token = token.strip()
    if token in scales:
        return scales[token]
    if token.lower().startswith("0x"):
        return float(int(token, 16))
    try:
        return float(token.rstrip("fF"))
    except ValueError as error:
        raise WeaponStatsError(f"stat value `{token}` is not a number or a known constant") from error


def _signed_byte(value: int) -> int:
    value &= 0xFF
    return value - 0x100 if value >= 0x80 else value


def _firing_columns(fields: Dict[str, str], scales: Dict[str, float]) -> Tuple[str, ...]:
    """The firing columns of one stats struct. The rates are signed bytes;
    `RecoilSpeed` is four of them (a union with the s32 the table writes, high
    byte first); the cartridge is the model's resource base path or `-`."""
    columns = [
        str(_signed_byte(int(_number(fields["AutomaticFiringRate"], scales)))),
        str(_signed_byte(int(_number(fields["SingleFiringRate"], scales)))),
    ]
    speed = int(_number(fields["RecoilSpeed"], scales)) & 0xFFFFFFFF
    columns += [str(_signed_byte(speed >> shift)) for shift in (24, 16, 8, 0)]
    columns += [repr(_number(fields["RecoilBack"], scales)), repr(_number(fields["RecoilUp"], scales))]
    cartridge = re.fullmatch(r"&(\w+)_header", fields["ptr_cartridge_struct"].strip())
    columns.append(f"models/gun/G{cartridge.group(1)}Z" if cartridge else "-")
    for field in _FIRING_FIELDS[6:]:
        columns.append(repr(_number(fields[field], scales)))
    return tuple(columns)


def parse_stats(decomp_dir: Path, ammo_values: Dict[str, int], flags: Dict[str, int]) -> Dict[str, Stats]:
    """Every `WeaponStats` definition's ammo type, magazine size, flags and placement."""
    gun_dir = decomp_dir / "assets" / "obseg" / "gun"
    text = _expand_includes(decomp_dir, _strip_comments((gun_dir / "gunWeaponStats.inc.c").read_text()))
    scales: Dict[str, float] = {}
    for name, value in _SCALE_DEFINE_RE.findall(text):
        scales.setdefault(name, float(int(value, 16)) if value.lower().startswith("0x") else float(value))
    text = re.sub(r"^\s*#(?!\s*include).*$", "", text, flags=re.M)
    stats: Dict[str, Stats] = {}
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
        placement = tuple(_number(fields[field], scales) for field in _PLACEMENT_FIELDS)
        stats[name] = Stats(
            ammo_type,
            int(fields["MagSize"], 0),
            bits,
            placement,
            _firing_columns(fields, scales),
            _number(fields["ForceOfImpact"], scales),
        )
    return stats


def parse_item_stats(decomp_dir: Path) -> List[ItemRow]:
    """Per item in `gitem_structs` order, the name of its stats struct
    (None for an item flagged as having no model, which takes the defaults),
    whether the record has a model header and the model it loads (a `GUNFILERECORD`
    names `models/gun/G<name>Z`, a `SUIT_LFRECORD` `models/chr/C<name>Z`)."""
    gun_dir = decomp_dir / "assets" / "obseg" / "gun"
    table = _expand_includes(decomp_dir, _strip_comments((gun_dir / "gunModelFileRecord.inc.c").read_text()))
    table = table[table.index("gitem_structs") :]
    rows: List[ItemRow] = []
    for match in _ROW_RE.finditer(table):
        nomodel = match.group("macro_nomodel") or match.group("row_nomodel")
        stats = match.group("macro_stats") or match.group("row_stats")
        has_header = match.group("row_header") is None or match.group("row_header").startswith("&")
        model = None
        if match.group("macro") == "GUNFILERECORD":
            model = f"models/gun/G{match.group('macro_name')}Z"
        elif match.group("macro") == "SUIT_LFRECORD":
            model = f"models/chr/C{match.group('macro_name')}Z"
        if nomodel in ("TRUE", "1") or stats == "NULL":
            rows.append(ItemRow(None, has_header, model))
        else:
            rows.append(ItemRow(_STATS_NAME_RE.fullmatch(stats).group(1) + "_stats", has_header, model))
    return rows


def parse_skeletons(decomp_dir: Path) -> Dict[str, Optional[str]]:
    """The skeleton each model header names, keyed by the header's name."""
    try:
        return {name: counts.skeleton_name for name, counts in read_model_header_counts(decomp_dir).items()}
    except GeModelError as error:
        raise WeaponStatsError(str(error)) from error


class WatchRow(NamedTuple):
    """The fields of one `gitem_structs` row the solo watch menu reads."""

    upper_text: int  # upper_watch_text: the first title line
    lower_text: int  # lower_watch_text: the second title line
    position: Tuple[float, float, float]  # watch_pos_x/y/z: where the item sits on the status page
    rotation: Tuple[float, float]  # x_rotation, y_rotation
    choice_text: int  # weapon_of_choice_text: the long name
    equipment_text: int  # watch_equipment_text: the short name
    equip: Tuple[float, float, float]  # equip_watch_x/y/z: where it sits on the inventory page


_GUN_ROW_RE = re.compile(r"\b(GUNFILERECORD|SUIT_LFRECORD)\s*\(|\{")
_STRING_ID_RE = re.compile(r"getStringID\(\s*(\w+)\s*,\s*(\w+)\s*\)")
# Where the watch fields start among a macro row's arguments
# (GUNFILERECORD(NAME, NOMODEL, STATS, UPPERTEXTID, ...)) and a literal row's
# ({ header, name, nomodel, stats, upper, ... }).
_MACRO_WATCH_FIRST = 3
_LITERAL_WATCH_FIRST = 4
_WATCH_ARGUMENTS = 12


def _balanced(text: str, start: int, opener: str, closer: str) -> int:
    """The index just past the `closer` that matches the `opener` at `start`."""
    depth = 0
    for index in range(start, len(text)):
        if text[index] == opener:
            depth += 1
        elif text[index] == closer:
            depth -= 1
            if depth == 0:
                return index + 1
    raise WeaponStatsError("unbalanced brackets in the item table")


def _string_id_resolver(decomp_dir: Path, bondconstants: str) -> Callable[[str], int]:
    """Resolves a `getStringID(<bank>, <slot>)` or a literal to the string id."""
    slots: Dict[str, List[str]] = {}

    def slot_names(bank: str) -> List[str]:
        if bank not in slots:
            header = decomp_dir / "assets" / "obseg" / "text" / f"{bank[0]}{bank[1:].lower()}E.h"
            text = _strip_comments(header.read_text())
            body = text[text.index("{") + 1 : text.index("}")]
            slots[bank] = [item.strip() for item in body.split(",") if item.strip()]
        return slots[bank]

    def resolve(token: str) -> int:
        match = _STRING_ID_RE.fullmatch(token.strip())
        if match is None:
            return int(token, 0)
        bank, slot = match.groups()
        banks = _enum_names(bondconstants, "TEXTBANK_LEVEL_INDEX")
        if bank not in banks:
            raise WeaponStatsError(f"unknown text bank `{bank}`")
        return banks.index(bank) * 0x400 + slot_names(bank).index(slot)

    return resolve


def parse_watch_rows(decomp_dir: Path, bondconstants: str) -> List[WatchRow]:
    """Per item in `gitem_structs` order, the fields the solo watch menu shows
    it with: its title lines and names (as string ids) and where its model sits
    on the menu's pages."""
    gun_dir = decomp_dir / "assets" / "obseg" / "gun"
    table = _expand_includes(decomp_dir, _strip_comments((gun_dir / "gunModelFileRecord.inc.c").read_text()))
    table = table[table.index("gitem_structs") :]
    table = table[table.index("{") + 1 :]
    resolve = _string_id_resolver(decomp_dir, bondconstants)

    def number(token: str) -> float:
        return float(token.strip().rstrip("fF"))

    rows: List[WatchRow] = []
    position = 0
    while True:
        match = _GUN_ROW_RE.search(table, position)
        if match is None:
            break
        if match.group(1) is None:
            end = _balanced(table, match.start(), "{", "}")
            arguments = _split_top_level(table[match.end() : end - 1])
            first = _LITERAL_WATCH_FIRST
        else:
            end = _balanced(table, match.end() - 1, "(", ")")
            arguments = _split_top_level(table[match.end() : end - 1])
            first = _MACRO_WATCH_FIRST
        watch = arguments[first:]
        if len(watch) != _WATCH_ARGUMENTS:
            raise WeaponStatsError(f"an item row has {len(watch)} watch fields, expected {_WATCH_ARGUMENTS}")
        rows.append(
            WatchRow(
                resolve(watch[0]),
                resolve(watch[1]),
                (number(watch[2]), number(watch[3]), number(watch[4])),
                (number(watch[5]), number(watch[6])),
                resolve(watch[7]),
                resolve(watch[8]),
                (number(watch[9]), number(watch[10]), number(watch[11])),
            )
        )
        position = end
    return rows


class AmmoRow(NamedTuple):
    """One `ammo_related` row."""

    limit: int  # MaxAmmo
    icon: int  # IconImage's offset in the global image table, 0 for no icon
    icon_y_offset: float


_IMAGE_SEGMENT_OFFSET_MASK = 0xFFFF


def parse_ammo_rows(decomp_dir: Path) -> List[AmmoRow]:
    """`ammo_related`: each ammo type's carry limit and HUD icon."""
    text = _strip_comments((decomp_dir / "src" / "game" / "gun.c").read_text())
    match = re.search(r"AmmoStats\s+ammo_related\[[^\]]*\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if match is None:
        raise WeaponStatsError("no `ammo_related` table in gun.c")
    rows = []
    for row in re.findall(r"\{([^{}]*)\}", match.group(1)):
        fields = [field.strip() for field in row.split(",") if field.strip()]
        if len(fields) != 3:
            raise WeaponStatsError(f"an `ammo_related` row has {len(fields)} fields, expected 3")
        rows.append(
            AmmoRow(
                int(fields[0], 0),
                int(fields[1], 0) & _IMAGE_SEGMENT_OFFSET_MASK,
                float(fields[2].rstrip("fF")),
            )
        )
    return rows


def build_weapon_stats(decomp_dir: Union[str, Path]) -> bytes:
    """The resource's bytes: one `item` line per ITEM_IDS value, then one
    `ammo` line per ammo type (a type the enum does not name has `-` for its
    name). Layout: docs/formats/goldeneye-weapon-stats.md."""
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
    skeletons = parse_skeletons(decomp_dir)
    lines = []
    for index, (name, row) in enumerate(zip(item_names, rows)):
        key = row.stats_name if row.stats_name is not None else "default_weaponstats"
        if key not in stats:
            raise WeaponStatsError(f"{name} names stats `{key}`, which are not defined")
        item = stats[key]
        model = row.model or "-"
        # A model header is named for its file without the directory letter and `Z`.
        skeleton = (skeletons.get(row.model.rsplit("/", 1)[1][1:-1]) if row.model else None) or "-"
        placement = "\t".join(repr(value) for value in item.placement)
        firing = "\t".join(item.firing)
        lines.append(
            f"item\t{index}\t{name}\t{int(row.has_header)}\t{item.ammo_type}\t{item.magazine}\t{item.flags:#x}"
            f"\t{model}\t{skeleton}\t{placement}\t{firing}"
        )
    watch_rows = parse_watch_rows(decomp_dir, bondconstants)
    if len(watch_rows) != len(item_names):
        raise WeaponStatsError(f"ITEM_IDS names {len(item_names)} items but the watch table has {len(watch_rows)} rows")
    for index, watch in enumerate(watch_rows):
        fields = [f"{watch.upper_text:#x}", f"{watch.lower_text:#x}"]
        fields += [repr(value) for value in watch.position + watch.rotation]
        fields += [f"{watch.choice_text:#x}", f"{watch.equipment_text:#x}"]
        fields += [repr(value) for value in watch.equip]
        lines.append(f"watch\t{index}\t" + "\t".join(fields))
    for index, row in enumerate(parse_ammo_rows(decomp_dir)):
        name = ammo_names[index] if index < len(ammo_names) else "-"
        lines.append(f"ammo\t{index}\t{name}\t{row.limit}\t{row.icon:#x}\t{row.icon_y_offset!r}")
    return ("\n".join(lines) + "\n").encode("ascii")


def item_forces_of_impact(decomp_dir: Union[str, Path]) -> List[float]:
    """`ForceOfImpact` of each `ITEM_IDS` value's stats, in item order."""
    decomp_dir = Path(decomp_dir)
    bondconstants = (decomp_dir / "src" / "bondconstants.h").read_text()
    ammo_values = {name: index for index, name in enumerate(_enum_names(bondconstants, "AMMOTYPE"))}
    stats = parse_stats(decomp_dir, ammo_values, parse_flag_values(bondconstants))
    return [
        stats[row.stats_name if row.stats_name is not None else "default_weaponstats"].force_of_impact
        for row in parse_item_stats(decomp_dir)
    ]


def pack_weapon_stats(decomp_dir: Union[str, Path], writer: O2rWriter) -> str:
    writer.add_resource(RESOURCE_PATH, "OBLB", version=0, resource_id=0, payload=build_weapon_stats(decomp_dir))
    return RESOURCE_PATH
