#!/usr/bin/env python3
"""Packs the characters' combat tables: the animations a hit character plays,
the animations a character fires its weapon in, and the explosion deaths.

The GoldenEye decomp's chr.c keeps them as C arrays: `StruckAnim` lists (the
death and flinch animations of each body part, reached through
`g_HitReactionTable`), `weapon_firing_animation_table` lists (the firing
animations, grouped by the angle to the target and by weapon stance, and the
rolling and moving ones), the explosion death animations and the table that
picks among them. They name clips as `PTR_ANIM_<name>`, which is the clip's
`animations/<name>` resource. Layout: docs/formats/goldeneye-chr-combat.md.
"""

from __future__ import annotations

import re
import struct
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union

from o2r_writer import O2rWriter
from pack_weapon_stats import item_forces_of_impact

RESOURCE_PATH = "chr/combat"

# Each pointer array names this many groups, one per 1/32 of a turn.
GROUPS_PER_SET = 32

_STRUCK_RE = re.compile(r"struct\s+StruckAnim\s+(\w+)\s*\[\s*\]\s*=\s*\{")
_FIRING_RE = re.compile(r"struct\s+weapon_firing_animation_table\s+(\w+)\s*\[\s*\]\s*=\s*\{")
_GROUP_RE = re.compile(r"struct\s+anim_group_info\s+(ptr_\w+)\s*=\s*\{\s*&(\w+)\s*,\s*-1\s*\}\s*;")
_GROUPS_RE = re.compile(r"struct\s+anim_group_info\s*\*\s*(ptr_\w+_groups)\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;", re.S)
_REACTION_RE = re.compile(r"struct\s+ChrHitReaction\s+g_HitReactionTable\s*\[\s*\]\s*=\s*\{")
_EXPLOSION_DEATH_RE = re.compile(r"struct\s+explosion_death_animation\s+\w+\s*\[\s*\]\s*=\s*\{")
_EXPLOSION_ROW_RE = re.compile(r"s8\s+(expl_\w+)\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;", re.S)
_EXPLOSION_TABLE_RE = re.compile(r"struct\s+explosion_anim_group_info\s+explosion_animation_table\s*\[[^\]]*\]\s*=\s*\{")

_MULTI_RE = re.compile(r"struct\s+firing_anim_struct\s+firing_animation_groups\s*\[\s*\]\s*\[\s*6\s*\]\s*=\s*\{")
_ANIM_DATA_RE = re.compile(r"u32\s+ANIM_DATA_(\w+)\s*\[\s*\]\s*=\s*\{(.*?)\};", re.S)
_TABLE_ENTRY_RE = re.compile(r"^&\s*(\w+)\s*\[\s*(\d+)\s*\]$")

_MULTI_FIELDS = 5
_STRUCK_FIELDS = 7
_FIRING_FIELDS = 18
_REACTION_FIELDS = 11
_EXPLOSION_DEATH_FIELDS = 7


class ChrCombatError(ValueError):
    """Raised when chr.c does not hold the tables."""


def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def _balanced(text: str, open_at: int) -> Tuple[str, int]:
    """The text between the brace at `open_at` and its match, and the index
    after the match."""
    depth = 0
    for index in range(open_at, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1 : index], index + 1
    raise ChrCombatError("unbalanced braces")


def _rows(body: str) -> List[List[str]]:
    """The brace-delimited rows of an initializer, each as its fields."""
    rows: List[List[str]] = []
    index = 0
    while True:
        start = body.find("{", index)
        if start < 0:
            return rows
        inner, index = _balanced(body, start)
        rows.append([field.strip() for field in inner.split(",") if field.strip()])


def _float(token: str) -> float:
    """The single-precision value a C float initializer holds."""
    value = struct.unpack("<f", struct.pack("<f", float(token.rstrip("fF"))))[0]
    return value


def _fmt(value: float) -> str:
    return repr(value)


def _anim(token: str) -> Optional[str]:
    if token == "0":
        return None
    if not token.startswith("PTR_ANIM_"):
        raise ChrCombatError(f"`{token}` is not an animation")
    return token[len("PTR_ANIM_") :]


def _int(token: str) -> int:
    return int(token, 0)


def parse_struck(source: str) -> Dict[str, List[Tuple[str, ...]]]:
    """List name -> its entries, up to the terminator: (anim, flip, endframe,
    speed, knockback, thudframe1, thudframe2)."""
    lists: Dict[str, List[Tuple[str, ...]]] = {}
    for match in _STRUCK_RE.finditer(source):
        body, _ = _balanced(source, match.end() - 1)
        entries: List[Tuple[str, ...]] = []
        for fields in _rows(body):
            if len(fields) != _STRUCK_FIELDS:
                raise ChrCombatError(f"{match.group(1)} has a row of {len(fields)} fields")
            anim = _anim(fields[0])
            if anim is None:
                break
            entries.append(
                (
                    anim,
                    str(_int(fields[1])),
                    _fmt(_float(fields[2])),
                    _fmt(_float(fields[3])),
                    str(_int(fields[4])),
                    _fmt(_float(fields[5])),
                    _fmt(_float(fields[6])),
                )
            )
        lists[match.group(1)] = entries
    return lists


def parse_firing(source: str) -> Dict[str, List[Tuple[str, ...]]]:
    """Table name -> its entries, as initResolveAnimGroupTable counts them. A
    table whose last row is not the terminator runs on into the table that
    follows it in memory, as the game's does."""
    names: List[str] = []
    raw: Dict[str, List[List[str]]] = {}
    for match in _FIRING_RE.finditer(source):
        body, _ = _balanced(source, match.end() - 1)
        names.append(match.group(1))
        raw[match.group(1)] = _rows(body)

    tables: Dict[str, List[Tuple[str, ...]]] = {}
    for position, name in enumerate(names):
        entries: List[Tuple[str, ...]] = []
        follow = position
        rows = raw[name]
        row = 0
        while True:
            if row >= len(rows):
                follow += 1
                if follow >= len(names):
                    raise ChrCombatError(f"{name} has no terminator")
                rows = raw[names[follow]]
                row = 0
                continue
            fields = rows[row]
            if len(fields) != _FIRING_FIELDS:
                raise ChrCombatError(f"{name} has a row of {len(fields)} fields")
            anim = _anim(fields[0])
            if anim is None:
                break
            entries.append((anim, *(_fmt(_float(field)) for field in fields[1:])))
            row += 1
        tables[name] = entries
    return tables


def parse_groups(source: str) -> Dict[str, List[str]]:
    """Set name (`rifle`, `crouched_pistol`, ...) -> the table each of its 32
    groups uses."""
    group_table = {match.group(1): match.group(2) for match in _GROUP_RE.finditer(source)}
    sets: Dict[str, List[str]] = {}
    for match in _GROUPS_RE.finditer(source):
        name = match.group(1)
        if not name.startswith("ptr_") or not name.endswith("_firing_animation_groups"):
            continue
        tables = []
        for entry in re.findall(r"&\s*(\w+)", match.group(2)):
            if entry not in group_table:
                raise ChrCombatError(f"{name} names the unknown group {entry}")
            tables.append(group_table[entry])
        if len(tables) != GROUPS_PER_SET:
            raise ChrCombatError(f"{name} has {len(tables)} groups, not {GROUPS_PER_SET}")
        sets[name[len("ptr_") : -len("_firing_animation_groups")]] = tables
    return sets


def animation_offsets(animation_data: str) -> Dict[int, str]:
    """Byte offset of each clip's header in the animation table (the value the
    game adds to `ptr_animation_table`) -> the clip's name."""
    offsets: Dict[int, str] = {}
    at = 0
    for match in _ANIM_DATA_RE.finditer(animation_data):
        offsets[at] = match.group(1)
        at += 4 * len([token for token in match.group(2).split(",") if token.strip()])
    return offsets


def parse_multi_firing(
    source: str, firing: Dict[str, List[Tuple[str, ...]]], offsets: Dict[int, str]
) -> List[Tuple[str, ...]]:
    """firing_animation_groups, the animations a multiplayer player's character
    plays for the other players: for each of the four groups and six stances,
    (table, index, animation, x, y, z). A row names a firing table entry
    (`table, index`, animation `-`) or just an animation (table `-`)."""
    match = _MULTI_RE.search(source)
    if match is None:
        raise ChrCombatError("bondview.c does not define firing_animation_groups")
    body, _ = _balanced(source, match.end() - 1)
    rows: List[Tuple[str, ...]] = []
    for group in _rows_of_rows(body):
        for fields in group:
            if len(fields) != _MULTI_FIELDS:
                raise ChrCombatError(f"firing_animation_groups has a row of {len(fields)} fields")
            pointer, anim = fields[0], _int(fields[1])
            if pointer == "NULL":
                if anim not in offsets:
                    raise ChrCombatError(f"firing_animation_groups names the unknown animation {anim:#x}")
                table, index, name = "-", "0", offsets[anim]
            else:
                entry = _TABLE_ENTRY_RE.match(pointer)
                table, index = (entry.group(1), entry.group(2)) if entry else (pointer, "0")
                if table not in firing or int(index) >= len(firing[table]):
                    raise ChrCombatError(f"firing_animation_groups names the unknown table {pointer}")
                if anim != 0:
                    raise ChrCombatError("firing_animation_groups row has both a table and an animation")
                name = "-"
            rows.append((table, index, name, *(_fmt(_float(field)) for field in fields[2:])))
    return rows


def _rows_of_rows(body: str) -> List[List[List[str]]]:
    """The braces of an initializer of initializers, each as its rows."""
    groups: List[List[List[str]]] = []
    index = 0
    while True:
        start = body.find("{", index)
        if start < 0:
            return groups
        inner, index = _balanced(body, start)
        groups.append(_rows(inner))


def parse_reactions(source: str, struck: Dict[str, List[Tuple[str, ...]]]) -> List[Tuple[str, ...]]:
    """g_HitReactionTable's rows up to the terminator: (hitpart, impact puff
    count, size, back puff count, size, death list or `-`, flinch list or `-`)."""
    match = _REACTION_RE.search(source)
    if match is None:
        raise ChrCombatError("chr.c does not define g_HitReactionTable")
    body, _ = _balanced(source, match.end() - 1)
    reactions: List[Tuple[str, ...]] = []
    for fields in _rows(body):
        if len(fields) != _REACTION_FIELDS:
            raise ChrCombatError(f"g_HitReactionTable has a row of {len(fields)} fields")
        part = _int(fields[0])
        if part == 0xFFFFFFFF:
            break
        lists = []
        for token in (fields[7], fields[9]):
            if token == "NULL":
                lists.append("-")
            elif token in struck:
                lists.append(token)
            else:
                raise ChrCombatError(f"g_HitReactionTable names the unknown list {token}")
        reactions.append(
            (
                str(part),
                str(_int(fields[1])),
                _fmt(_float(fields[3])),
                str(_int(fields[4])),
                _fmt(_float(fields[6])),
                lists[0],
                lists[1],
            )
        )
    return reactions


def parse_explosion_deaths(source: str) -> List[Tuple[str, ...]]:
    """The explosion death animations: (anim, flip, speed, start frame, the
    unused frame, thud frame, end frame)."""
    match = _EXPLOSION_DEATH_RE.search(source)
    if match is None:
        raise ChrCombatError("chr.c does not define the explosion death animations")
    body, _ = _balanced(source, match.end() - 1)
    deaths: List[Tuple[str, ...]] = []
    for fields in _rows(body):
        if len(fields) != _EXPLOSION_DEATH_FIELDS:
            raise ChrCombatError(f"an explosion death has a row of {len(fields)} fields")
        anim = _anim(fields[0])
        if anim is None:
            break
        deaths.append((anim, str(_int(fields[1])), *(_fmt(_float(field)) for field in fields[2:])))
    return deaths


def parse_explosion_groups(source: str) -> List[List[int]]:
    """For each of the eight directions, the explosion deaths it picks from."""
    arrays = {
        match.group(1): [_int(token.strip()) for token in match.group(2).split(",") if token.strip()]
        for match in _EXPLOSION_ROW_RE.finditer(source)
    }
    match = _EXPLOSION_TABLE_RE.search(source)
    if match is None:
        raise ChrCombatError("chr.c does not define explosion_animation_table")
    body, _ = _balanced(source, match.end() - 1)
    groups: List[List[int]] = []
    for fields in _rows(body):
        if len(fields) != 2 or fields[0] not in arrays:
            raise ChrCombatError("explosion_animation_table has a malformed row")
        groups.append(arrays[fields[0]][: _int(fields[1])])
    return groups


def build_chr_combat(
    source: str,
    clips: Optional[set] = None,
    forces: Optional[List[float]] = None,
    multi_source: Optional[str] = None,
    offsets: Optional[Dict[int, str]] = None,
) -> bytes:
    """The resource's bytes. Layout: docs/formats/goldeneye-chr-combat.md."""
    source = _strip_comments(source)
    struck = parse_struck(source)
    firing = parse_firing(source)
    groups = parse_groups(source)
    reactions = parse_reactions(source, struck)
    deaths = parse_explosion_deaths(source)
    explosion_groups = parse_explosion_groups(source)

    lines: List[str] = []
    for name, entries in struck.items():
        for index, entry in enumerate(entries):
            lines.append("\t".join(("struck", name, str(index), *entry)))
    for index, reaction in enumerate(reactions):
        lines.append("\t".join(("reaction", str(index), *reaction)))
    for name, entries in firing.items():
        for index, entry in enumerate(entries):
            lines.append("\t".join(("firing", name, str(index), *entry)))
    for name, tables in groups.items():
        for index, table in enumerate(tables):
            if table not in firing:
                raise ChrCombatError(f"{name} names the unknown table {table}")
            lines.append("\t".join(("group", name, str(index), table)))
    for index, death in enumerate(deaths):
        lines.append("\t".join(("explosiondeath", str(index), *death)))
    for index, group in enumerate(explosion_groups):
        lines.append("\t".join(("explosiongroup", str(index), *(str(entry) for entry in group))))

    if multi_source is not None:
        multi = parse_multi_firing(_strip_comments(multi_source), firing, offsets or {})
        for index, row in enumerate(multi):
            lines.append("\t".join(("multifiring", str(index // 6), str(index % 6), *row)))

    for item, force in enumerate(forces or []):
        if force != 0.0:
            lines.append(f"impact\t{item}\t{force!r}")

    if clips is not None:
        used = {entry[0] for entries in struck.values() for entry in entries}
        used |= {entry[0] for entries in firing.values() for entry in entries}
        used |= {death[0] for death in deaths}
        if multi_source is not None:
            used |= {row[2] for row in multi if row[2] != "-"}
        missing = sorted(name for name in used if name not in clips)
        if missing:
            raise ChrCombatError(f"animation {missing[0]} has no clip")
    return ("\n".join(lines) + "\n").encode("ascii")


def pack_chr_combat(decomp_dir: Union[str, Path], writer: O2rWriter, clips: Optional[set] = None) -> str:
    source = (Path(decomp_dir) / "src" / "game" / "chr.c").read_text()
    forces = item_forces_of_impact(decomp_dir)
    multi_source = (Path(decomp_dir) / "src" / "game" / "bondview.c").read_text()
    offsets = animation_offsets((Path(decomp_dir) / "assets" / "animationtable_data.c").read_text())
    writer.add_resource(
        RESOURCE_PATH,
        "OBLB",
        version=0,
        resource_id=0,
        payload=build_chr_combat(source, clips, forces, multi_source, offsets),
    )
    return RESOURCE_PATH
