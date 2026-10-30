"""The weapon table: item stats, watch menu placement and ammo types, per region."""

from __future__ import annotations

import re
import struct
from pathlib import Path
from typing import Callable, Dict, List, NamedTuple, Optional, Tuple

from .common import (REGION_ENUMERATORS, REGIONS, Decomp, Output, TableError, balanced, banner, cstr, cstr_or_null,
                     enum_body, footer, num, split_top_level, strip_comments)
from .models import model_header_name, read_model_headers

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
# The stat tables' SCALEVAL and RATE constants. The file defines each once per release; the region's build selects
# one set (BUGFIX_R0 for the US release, R2 for Europe, R1 for Japan).
_SCALE_DEFINE_RE = re.compile(r"^#define\s+((?:SCALEVAL|RATE_)\w*)\s+(0[xX][0-9a-fA-F]+|[0-9.]+)f?\s*$", re.M)

# The fields of `struct WeaponStats` in initializer order (the recoil speed union is one initializer).
_FIELDS = [
    "MuzzleFlashExtension", "PosX", "PosY", "PosZ", "PlayX", "PlayY", "PlayZ",
    "AmmoType", "MagSize", "AutomaticFiringRate", "SingleFiringRate",
    "ObjectsShootThrough", "SoundTriggerRate", "Sound", "ptr_cartridge_struct",
    "DestructionAmount", "Inaccuracy", "Zoom", "CrosshairSpeed", "AimLockSpeed",
    "Sway", "RecoilSpeed", "RecoilBack", "RecoilUp", "BoltRecoilBack",
    "LoudnessMin", "LoudnessMax", "NoiseIncreasePerShot", "NoiseDecayLinearTime",
    "NoiseDecayScaledTime", "ForceOfImpact", "BitFlags",
]  # fmt: skip

# The stat fields the level code places the held model with.
_PLACEMENT_FIELDS = [
    "MuzzleFlashExtension", "PosX", "PosY", "PosZ", "PlayX", "PlayY", "PlayZ",
    "CrosshairSpeed", "AimLockSpeed", "Sway", "BoltRecoilBack",
]  # fmt: skip

# The stat fields of the firing record that are plain floats, in order.
_FIRING_FLOATS = [
    "LoudnessMin", "LoudnessMax", "NoiseIncreasePerShot", "NoiseDecayLinearTime", "NoiseDecayScaledTime",
    "Zoom", "Inaccuracy", "ObjectsShootThrough", "DestructionAmount",
]  # fmt: skip


class Firing(NamedTuple):
    automatic_rate: int
    single_rate: int
    recoil_speed: Tuple[int, int, int, int]
    recoil_back: float
    recoil_up: float
    cartridge: Optional[str]
    floats: Tuple[float, ...]  # _FIRING_FLOATS order
    sound_trigger_rate: int
    sound: int


class Stats(NamedTuple):
    ammo_type: int
    magazine: int
    flags: int
    placement: Tuple[float, ...]  # _PLACEMENT_FIELDS order
    firing: Firing
    force_of_impact: float


class ItemRow(NamedTuple):
    stats_name: Optional[str]  # `<name>_stats`; None for an item flagged as having no model
    has_header: bool
    model: Optional[str]  # the model's resource base path, None without a model file


def _expand_includes(decomp: Decomp, text: str) -> str:
    return _INCLUDE_RE.sub(lambda m: strip_comments(decomp.read(m.group(1))), text)


def enum_names(bondconstants: str, name: str) -> List[str]:
    """The enumerators of `typedef enum <name> {...} <name>;`. An enumerator defined as an earlier enumerator
    (`AMMOTYPE_GLOBAL_MAX = AMMO_GGUN`) is an alias and takes no slot of its own."""
    names: List[str] = []
    for item in enum_body(bondconstants, name).split(","):
        item = item.strip()
        if item:
            label, _, assigned = item.partition("=")
            if assigned.strip() in names:
                continue
            names.append(label.strip())
    return names


def _number(token: str, scales: Dict[str, float]) -> float:
    token = token.strip()
    if token in scales:
        return scales[token]
    if token.lower().startswith("0x"):
        return float(int(token, 16))
    try:
        return float(token.rstrip("fF"))
    except ValueError as error:
        raise TableError(f"stat value `{token}` is not a number or a known constant") from error


def _signed_byte(value: int) -> int:
    value &= 0xFF
    return value - 0x100 if value >= 0x80 else value


def _firing(fields: Dict[str, str], scales: Dict[str, float]) -> Firing:
    """The rates are signed bytes; `RecoilSpeed` is four of them (a union with the s32 the table writes, high byte
    first); the cartridge is the model's header name."""
    speed = int(_number(fields["RecoilSpeed"], scales)) & 0xFFFFFFFF
    cartridge = re.fullmatch(r"&(\w+)_header", fields["ptr_cartridge_struct"].strip())
    return Firing(
        _signed_byte(int(_number(fields["AutomaticFiringRate"], scales))),
        _signed_byte(int(_number(fields["SingleFiringRate"], scales))),
        tuple(_signed_byte(speed >> shift) for shift in (24, 16, 8, 0)),
        _number(fields["RecoilBack"], scales),
        _number(fields["RecoilUp"], scales),
        f"models/gun/G{cartridge.group(1)}Z" if cartridge else None,
        tuple(_number(fields[field], scales) for field in _FIRING_FLOATS),
        int(_number(fields["SoundTriggerRate"], scales)),
        int(_number(fields["Sound"], scales)),
    )


def parse_stats(decomp: Decomp, ammo_values: Dict[str, int], flags: Dict[str, int]) -> Dict[str, Stats]:
    text = _expand_includes(decomp, strip_comments(decomp.read("assets/obseg/gun/gunWeaponStats.inc.c")))
    scales: Dict[str, float] = {}
    for name, value in _SCALE_DEFINE_RE.findall(text):
        scales.setdefault(name, float(int(value, 16)) if value.lower().startswith("0x") else float(value))
    text = re.sub(r"^\s*#(?!\s*include).*$", "", text, flags=re.M)
    stats: Dict[str, Stats] = {}
    for name, body in _STATS_RE.findall(text):
        values = split_top_level(body)
        if len(values) != len(_FIELDS):
            raise TableError(f"{name} has {len(values)} initializers, expected {len(_FIELDS)}")
        fields = dict(zip(_FIELDS, values))
        ammo = fields["AmmoType"]
        ammo_type = ammo_values[ammo] if ammo in ammo_values else int(ammo, 0)
        bits = 0
        for flag in re.split(r"\|", fields["BitFlags"]):
            flag = flag.strip()
            if flag:
                bits |= flags[flag] if flag in flags else int(flag, 0)
        stats[name] = Stats(
            ammo_type,
            int(fields["MagSize"], 0),
            bits,
            tuple(_number(fields[field], scales) for field in _PLACEMENT_FIELDS),
            _firing(fields, scales),
            _number(fields["ForceOfImpact"], scales),
        )
    return stats


def parse_item_stats(decomp: Decomp) -> List[ItemRow]:
    """Per item in `gitem_structs` order, the name of its stats struct (None for an item flagged as having no model,
    which takes the defaults), whether the record has a model header and the model it loads."""
    table = _expand_includes(decomp, strip_comments(decomp.read("assets/obseg/gun/gunModelFileRecord.inc.c")))
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


class WatchRow(NamedTuple):
    upper_text: int
    lower_text: int
    position: Tuple[float, float, float]
    rotation: Tuple[float, float]
    choice_text: int
    equipment_text: int
    equip: Tuple[float, float, float]


_GUN_ROW_RE = re.compile(r"\b(GUNFILERECORD|SUIT_LFRECORD)\s*\(|\{")
_STRING_ID_RE = re.compile(r"getStringID\(\s*(\w+)\s*,\s*(\w+)\s*\)")
# Where the watch fields start among a macro row's arguments (GUNFILERECORD(NAME, NOMODEL, STATS, UPPERTEXTID, ...))
# and a literal row's ({ header, name, nomodel, stats, upper, ... }).
_MACRO_WATCH_FIRST = 3
_LITERAL_WATCH_FIRST = 4
_WATCH_ARGUMENTS = 12


def _string_id_resolver(decomp: Decomp, bondconstants: str) -> Callable[[str], int]:
    """Resolves a `getStringID(<bank>, <slot>)` or a literal to the string id."""
    slots: Dict[str, List[str]] = {}

    def slot_names(bank: str) -> List[str]:
        if bank not in slots:
            text = strip_comments(decomp.read(f"assets/obseg/text/{bank[0]}{bank[1:].lower()}E.h"))
            body = text[text.index("{") + 1 : text.index("}")]
            slots[bank] = [item.strip() for item in body.split(",") if item.strip()]
        return slots[bank]

    def resolve(token: str) -> int:
        match = _STRING_ID_RE.fullmatch(token.strip())
        if match is None:
            return int(token, 0)
        bank, slot = match.groups()
        banks = enum_names(bondconstants, "TEXTBANK_LEVEL_INDEX")
        if bank not in banks:
            raise TableError(f"unknown text bank `{bank}`")
        return banks.index(bank) * 0x400 + slot_names(bank).index(slot)

    return resolve


def parse_watch_rows(decomp: Decomp, bondconstants: str) -> List[WatchRow]:
    table = _expand_includes(decomp, strip_comments(decomp.read("assets/obseg/gun/gunModelFileRecord.inc.c")))
    table = table[table.index("gitem_structs") :]
    table = table[table.index("{") + 1 :]
    resolve = _string_id_resolver(decomp, bondconstants)

    def number(token: str) -> float:
        return float(token.strip().rstrip("fF"))

    rows: List[WatchRow] = []
    position = 0
    while True:
        match = _GUN_ROW_RE.search(table, position)
        if match is None:
            break
        if match.group(1) is None:
            inner, end = balanced(table, match.start(), "{", "}")
            arguments = split_top_level(inner)
            first = _LITERAL_WATCH_FIRST
        else:
            inner, end = balanced(table, match.end() - 1, "(", ")")
            arguments = split_top_level(inner)
            first = _MACRO_WATCH_FIRST
        watch = arguments[first:]
        if len(watch) != _WATCH_ARGUMENTS:
            raise TableError(f"an item row has {len(watch)} watch fields, expected {_WATCH_ARGUMENTS}")
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
    name: Optional[str]
    limit: int
    icon: int
    icon_y_offset: float


_IMAGE_SEGMENT_OFFSET_MASK = 0xFFFF


def parse_ammo_rows(decomp: Decomp, ammo_names: List[str]) -> List[AmmoRow]:
    """`ammo_related`: each ammo type's carry limit and HUD icon."""
    text = strip_comments(decomp.read("src/game/gun.c"))
    match = re.search(r"AmmoStats\s+ammo_related\[[^\]]*\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if match is None:
        raise TableError("no `ammo_related` table in gun.c")
    rows = []
    for index, row in enumerate(re.findall(r"\{([^{}]*)\}", match.group(1))):
        fields = [field.strip() for field in row.split(",") if field.strip()]
        if len(fields) != 3:
            raise TableError(f"an `ammo_related` row has {len(fields)} fields, expected 3")
        rows.append(
            AmmoRow(
                ammo_names[index] if index < len(ammo_names) else None,
                int(fields[0], 0),
                int(fields[1], 0) & _IMAGE_SEGMENT_OFFSET_MASK,
                float(fields[2].rstrip("fF")),
            )
        )
    return rows


class Weapons(NamedTuple):
    items: List[Tuple[str, ItemRow, Stats, Optional[str]]]  # name, row, stats, skeleton
    watch: List[WatchRow]
    ammo: List[AmmoRow]


def read_weapons(decomp: Decomp) -> Weapons:
    bondconstants = decomp.read("src/bondconstants.h")
    ammo_names = enum_names(bondconstants, "AMMOTYPE")
    ammo_values = {name: index for index, name in enumerate(ammo_names)}
    item_names = [n for n in enum_names(bondconstants, "ITEM_IDS") if n not in ("ITEM_NOTHING", "ITEM_IDS_MAX")]
    flags = {name: int(value, 0) for name, value in _DEFINE_RE.findall(bondconstants)}
    stats = parse_stats(decomp, ammo_values, flags)
    if "default_weaponstats" not in stats:
        raise TableError("no `default_weaponstats`")
    rows = parse_item_stats(decomp)
    if len(rows) != len(item_names):
        raise TableError(f"ITEM_IDS names {len(item_names)} items but the table has {len(rows)} rows")
    headers = read_model_headers(decomp)
    items = []
    for name, row in zip(item_names, rows):
        key = row.stats_name if row.stats_name is not None else "default_weaponstats"
        if key not in stats:
            raise TableError(f"{name} names stats `{key}`, which are not defined")
        header = headers.get(model_header_name(row.model.rsplit("/", 1)[1])) if row.model else None
        items.append((name, row, stats[key], header.skeleton if header else None))
    watch = parse_watch_rows(decomp, bondconstants)
    if len(watch) != len(item_names):
        raise TableError(f"ITEM_IDS names {len(item_names)} items but the watch table has {len(watch)} rows")
    return Weapons(items, watch, parse_ammo_rows(decomp, ammo_names))


def _item_lines(weapons: Weapons) -> List[str]:
    lines = []
    for index, (name, row, stats, skeleton) in enumerate(weapons.items):
        f = stats.firing
        placement = ", ".join(num(v) for v in stats.placement)
        speed = ", ".join(str(v) for v in f.recoil_speed)
        floats = ", ".join(num(v) for v in f.floats)
        lines.append(f"    // {index}: {name}")
        lines.append(f"    {{{cstr(name)}, {int(row.has_header)}, {stats.ammo_type}, {stats.magazine}, "
                     f"{stats.flags:#x}, {cstr_or_null(row.model)}, {cstr_or_null(skeleton)},")
        lines.append(f"     {{{placement}}},")
        lines.append(f"     {{{f.automatic_rate}, {f.single_rate}, {{{speed}}}, {num(f.recoil_back)}, "
                     f"{num(f.recoil_up)}, {cstr_or_null(f.cartridge)},")
        lines.append(f"      {{{floats}}}, {f.sound_trigger_rate}, {f.sound}}}, {num(stats.force_of_impact)}}},")
    return lines


def generate_weapon_stats(root: Path) -> Output:
    out = banner("the GoldenEye decomp's weapon tables (assets/obseg/gun/gunWeaponStats.inc.c, "
                 "gunModelFileRecord.inc.c, src/game/gun.c, ITEM_IDS and AMMOTYPE in src/bondconstants.h)")
    out += ['#include "extract/DecompTables.h"', "", "namespace Frigate::Extract::Detail {", ""]
    out.append("// The three releases differ only in the SCALEVAL and RATE constants of gunWeaponStats.inc.c.")
    out.append("")
    for region in REGIONS:
        decomp = Decomp(root, region)
        weapons = read_weapons(decomp)
        suffix = REGION_ENUMERATORS[region]
        out.append(f"// {region.upper()}: {{ITEM_ name, hasHeader, ammo type, magazine, flags, model, skeleton,")
        out.append("//  {mounting: muzzle flash extension, pos x/y/z, play x/y/z, crosshair speed, aim lock speed, sway,")
        out.append("//   bolt recoil},")
        out.append("//  {automatic rate, single rate, {recoil speed bytes}, recoil back, recoil up, cartridge model,")
        out.append("//   {loudness min/max, noise per shot, noise decay linear/scaled, zoom, inaccuracy, objects shot")
        out.append("//   through, destruction}, sound trigger rate, sound}, force of impact}")
        out.append(f"const WeaponItem kWeaponItems{suffix}[] = {{")
        out += _item_lines(weapons)
        out += ["};", ""]
        out.append(f"// {region.upper()}: {{upper text, lower text, {{position x/y/z, rotation x/y}}, weapon-of-choice text,")
        out.append("//  equipment text, equip position}; text ids are bank * 0x400 + slot.")
        out.append(f"const WeaponWatch kWeaponWatch{suffix}[] = {{")
        for name_row, w in zip(weapons.items, weapons.watch):
            pos = ", ".join(num(v) for v in w.position + w.rotation)
            equip = ", ".join(num(v) for v in w.equip)
            out.append(f"    {{{w.upper_text:#x}, {w.lower_text:#x}, {{{pos}}}, {w.choice_text:#x}, "
                       f"{w.equipment_text:#x}, {{{equip}}}}}, // {name_row[0]}")
        out += ["};", ""]
        out.append(f"// {region.upper()}: {{AMMOTYPE name, carry limit, HUD icon offset, icon y offset}}")
        out.append(f"const WeaponAmmo kWeaponAmmo{suffix}[] = {{")
        for a in weapons.ammo:
            out.append(f"    {{{cstr_or_null(a.name)}, {a.limit}, {a.icon:#x}, {num(a.icon_y_offset)}}},")
        out += ["};", ""]
        out.append(f"const WeaponTables kWeaponTables{suffix} = {{kWeaponItems{suffix}, "
                   f"sizeof(kWeaponItems{suffix}) / sizeof(kWeaponItems{suffix}[0]), kWeaponWatch{suffix}, "
                   f"sizeof(kWeaponWatch{suffix}) / sizeof(kWeaponWatch{suffix}[0]), kWeaponAmmo{suffix}, "
                   f"sizeof(kWeaponAmmo{suffix}) / sizeof(kWeaponAmmo{suffix}[0])}};")
        out.append("")
    out += ["}  // namespace Frigate::Extract::Detail"] + footer()
    return Output("src/extract/WeaponStatsData.cpp", "\n".join(out))
