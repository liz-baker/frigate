"""The characters' combat tables: hit reactions, firing animations and explosion deaths (src/game/chr.c), the
multiplayer firing animations (src/game/bondview.c), and the animation id table (src/game/initanitable.c)."""

from __future__ import annotations

import re
import struct
from typing import Dict, List, Optional, Tuple

from .common import Decomp, Output, TableError, balanced, banner, cstr, cstr_or_null, footer, num, strip_comments

# Each pointer array names this many groups, one per 1/32 of a turn.
GROUPS_PER_SET = 32

_STRUCK_RE = re.compile(r"struct\s+StruckAnim\s+(\w+)\s*\[\s*\]\s*=\s*\{")
_FIRING_RE = re.compile(r"struct\s+weapon_firing_animation_table\s+(\w+)\s*\[\s*\]\s*=\s*\{")
_GROUP_RE = re.compile(r"struct\s+anim_group_info\s+(ptr_\w+)\s*=\s*\{\s*&(\w+)\s*,\s*-1\s*\}\s*;")
_GROUPS_RE = re.compile(r"struct\s+anim_group_info\s*\*\s*(ptr_\w+_groups)\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;", re.S)
_REACTION_RE = re.compile(r"struct\s+ChrHitReaction\s+g_HitReactionTable\s*\[\s*\]\s*=\s*\{")
_EXPLOSION_DEATH_RE = re.compile(r"struct\s+explosion_death_animation\s+\w+\s*\[\s*\]\s*=\s*\{")
_EXPLOSION_ROW_RE = re.compile(r"s8\s+(expl_\w+)\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;", re.S)
_EXPLOSION_TABLE_RE = re.compile(
    r"struct\s+explosion_anim_group_info\s+explosion_animation_table\s*\[[^\]]*\]\s*=\s*\{")
_MULTI_RE = re.compile(r"struct\s+firing_anim_struct\s+firing_animation_groups\s*\[\s*\]\s*\[\s*6\s*\]\s*=\s*\{")
_ANIM_DATA_RE = re.compile(r"u32\s+ANIM_DATA_(\w+)\s*\[\s*\]\s*=\s*\{(.*?)\};", re.S)
_TABLE_ENTRY_RE = re.compile(r"^&\s*(\w+)\s*\[\s*(\d+)\s*\]$")

_MULTI_FIELDS = 5
_STRUCK_FIELDS = 7
_FIRING_FIELDS = 18
_REACTION_FIELDS = 11
_EXPLOSION_DEATH_FIELDS = 7
# The longest list of explosion deaths a direction picks from that DecompTables.h has room for.
EXPLOSION_GROUP_CAPACITY = 16


def _rows(body: str) -> List[List[str]]:
    """The brace-delimited rows of an initializer, each as its fields."""
    rows: List[List[str]] = []
    index = 0
    while True:
        start = body.find("{", index)
        if start < 0:
            return rows
        inner, index = balanced(body, start)
        rows.append([field.strip() for field in inner.split(",") if field.strip()])


def _rows_of_rows(body: str) -> List[List[List[str]]]:
    groups: List[List[List[str]]] = []
    index = 0
    while True:
        start = body.find("{", index)
        if start < 0:
            return groups
        inner, index = balanced(body, start)
        groups.append(_rows(inner))


def _float(token: str) -> float:
    """The single-precision value a C float initializer holds."""
    return struct.unpack("<f", struct.pack("<f", float(token.rstrip("fF"))))[0]


def _anim(token: str) -> Optional[str]:
    if token == "0":
        return None
    if not token.startswith("PTR_ANIM_"):
        raise TableError(f"`{token}` is not an animation")
    return token[len("PTR_ANIM_") :]


def _int(token: str) -> int:
    return int(token, 0)


def parse_struck(source: str) -> Dict[str, List[tuple]]:
    """List name -> its entries, up to the terminator: (anim, flip, endframe, speed, knockback, thudframe1,
    thudframe2)."""
    lists: Dict[str, List[tuple]] = {}
    for match in _STRUCK_RE.finditer(source):
        body, _ = balanced(source, match.end() - 1)
        entries = []
        for fields in _rows(body):
            if len(fields) != _STRUCK_FIELDS:
                raise TableError(f"{match.group(1)} has a row of {len(fields)} fields")
            anim = _anim(fields[0])
            if anim is None:
                break
            entries.append((anim, _int(fields[1]), _float(fields[2]), _float(fields[3]), _int(fields[4]),
                            _float(fields[5]), _float(fields[6])))
        lists[match.group(1)] = entries
    return lists


def parse_firing(source: str) -> Dict[str, List[tuple]]:
    """Table name -> its entries (anim, 17 floats), as initResolveAnimGroupTable counts them. A table whose last row
    is not the terminator runs on into the table that follows it in memory, as the game's does."""
    names: List[str] = []
    raw: Dict[str, List[List[str]]] = {}
    for match in _FIRING_RE.finditer(source):
        body, _ = balanced(source, match.end() - 1)
        names.append(match.group(1))
        raw[match.group(1)] = _rows(body)

    tables: Dict[str, List[tuple]] = {}
    for position, name in enumerate(names):
        entries = []
        follow = position
        rows = raw[name]
        row = 0
        while True:
            if row >= len(rows):
                follow += 1
                if follow >= len(names):
                    raise TableError(f"{name} has no terminator")
                rows = raw[names[follow]]
                row = 0
                continue
            fields = rows[row]
            if len(fields) != _FIRING_FIELDS:
                raise TableError(f"{name} has a row of {len(fields)} fields")
            anim = _anim(fields[0])
            if anim is None:
                break
            entries.append((anim, *(_float(field) for field in fields[1:])))
            row += 1
        tables[name] = entries
    return tables


def parse_groups(source: str) -> Dict[str, List[str]]:
    """Set name (`rifle`, `crouched_pistol`, ...) -> the table each of its 32 groups uses."""
    group_table = {match.group(1): match.group(2) for match in _GROUP_RE.finditer(source)}
    sets: Dict[str, List[str]] = {}
    for match in _GROUPS_RE.finditer(source):
        name = match.group(1)
        if not name.startswith("ptr_") or not name.endswith("_firing_animation_groups"):
            continue
        tables = []
        for entry in re.findall(r"&\s*(\w+)", match.group(2)):
            if entry not in group_table:
                raise TableError(f"{name} names the unknown group {entry}")
            tables.append(group_table[entry])
        if len(tables) != GROUPS_PER_SET:
            raise TableError(f"{name} has {len(tables)} groups, not {GROUPS_PER_SET}")
        sets[name[len("ptr_") : -len("_firing_animation_groups")]] = tables
    return sets


def animation_offsets(animation_data: str) -> Dict[int, str]:
    """Byte offset of each clip's header in the animation table (the value the game adds to `ptr_animation_table`)
    -> the clip's name."""
    offsets: Dict[int, str] = {}
    at = 0
    for match in _ANIM_DATA_RE.finditer(animation_data):
        offsets[at] = match.group(1)
        at += 4 * len([token for token in match.group(2).split(",") if token.strip()])
    return offsets


def parse_multi_firing(source: str, firing, offsets: Dict[int, str]) -> List[tuple]:
    """firing_animation_groups: for each of the four groups and six stances, (table, index, animation, x, y, z). A
    row names a firing table entry (`table, index`, animation None) or just an animation (table None)."""
    match = _MULTI_RE.search(source)
    if match is None:
        raise TableError("bondview.c does not define firing_animation_groups")
    body, _ = balanced(source, match.end() - 1)
    rows = []
    for group in _rows_of_rows(body):
        for fields in group:
            if len(fields) != _MULTI_FIELDS:
                raise TableError(f"firing_animation_groups has a row of {len(fields)} fields")
            pointer, anim = fields[0], _int(fields[1])
            if pointer == "NULL":
                if anim not in offsets:
                    raise TableError(f"firing_animation_groups names the unknown animation {anim:#x}")
                table, index, name = None, 0, offsets[anim]
            else:
                entry = _TABLE_ENTRY_RE.match(pointer)
                table, index = (entry.group(1), int(entry.group(2))) if entry else (pointer, 0)
                if table not in firing or index >= len(firing[table]):
                    raise TableError(f"firing_animation_groups names the unknown table {pointer}")
                if anim != 0:
                    raise TableError("firing_animation_groups row has both a table and an animation")
                name = None
            rows.append((table, index, name, *(_float(field) for field in fields[2:])))
    return rows


def parse_reactions(source: str, struck) -> List[tuple]:
    """g_HitReactionTable's rows up to the terminator: (hitpart, impact puff count, size, back puff count, size,
    death list or None, flinch list or None)."""
    match = _REACTION_RE.search(source)
    if match is None:
        raise TableError("chr.c does not define g_HitReactionTable")
    body, _ = balanced(source, match.end() - 1)
    reactions = []
    for fields in _rows(body):
        if len(fields) != _REACTION_FIELDS:
            raise TableError(f"g_HitReactionTable has a row of {len(fields)} fields")
        part = _int(fields[0])
        if part == 0xFFFFFFFF:
            break
        lists = []
        for token in (fields[7], fields[9]):
            if token == "NULL":
                lists.append(None)
            elif token in struck:
                lists.append(token)
            else:
                raise TableError(f"g_HitReactionTable names the unknown list {token}")
        reactions.append((part, _int(fields[1]), _float(fields[3]), _int(fields[4]), _float(fields[6]), *lists))
    return reactions


def parse_explosion_deaths(source: str) -> List[tuple]:
    """(anim, flip, speed, start frame, the unused frame, thud frame, end frame)."""
    match = _EXPLOSION_DEATH_RE.search(source)
    if match is None:
        raise TableError("chr.c does not define the explosion death animations")
    body, _ = balanced(source, match.end() - 1)
    deaths = []
    for fields in _rows(body):
        if len(fields) != _EXPLOSION_DEATH_FIELDS:
            raise TableError(f"an explosion death has a row of {len(fields)} fields")
        anim = _anim(fields[0])
        if anim is None:
            break
        deaths.append((anim, _int(fields[1]), *(_float(field) for field in fields[2:])))
    return deaths


def parse_explosion_groups(source: str) -> List[List[int]]:
    """For each of the eight directions, the explosion deaths it picks from."""
    arrays = {
        match.group(1): [_int(token.strip()) for token in match.group(2).split(",") if token.strip()]
        for match in _EXPLOSION_ROW_RE.finditer(source)
    }
    match = _EXPLOSION_TABLE_RE.search(source)
    if match is None:
        raise TableError("chr.c does not define explosion_animation_table")
    body, _ = balanced(source, match.end() - 1)
    groups = []
    for fields in _rows(body):
        if len(fields) != 2 or fields[0] not in arrays:
            raise TableError("explosion_animation_table has a malformed row")
        groups.append(arrays[fields[0]][: _int(fields[1])])
    return groups


def _anim_ref(name: Optional[str]) -> str:
    return cstr_or_null(name)


def generate_chr_combat(decomp: Decomp) -> Output:
    source = strip_comments(decomp.read("src/game/chr.c"))
    multi_source = strip_comments(decomp.read("src/game/bondview.c"))
    animation_data = decomp.read("assets/animationtable_data.c")
    clips = {match.group(1) for match in _ANIM_DATA_RE.finditer(animation_data)}

    struck = parse_struck(source)
    firing = parse_firing(source)
    groups = parse_groups(source)
    reactions = parse_reactions(source, struck)
    deaths = parse_explosion_deaths(source)
    explosion_groups = parse_explosion_groups(source)
    multi = parse_multi_firing(multi_source, firing, animation_offsets(animation_data))
    for name, tables in groups.items():
        for table in tables:
            if table not in firing:
                raise TableError(f"{name} names the unknown table {table}")
    for group in explosion_groups:
        if len(group) > EXPLOSION_GROUP_CAPACITY:
            raise TableError("an explosion direction picks from more deaths than DecompTables.h has room for")

    used = {entry[0] for entries in struck.values() for entry in entries}
    used |= {entry[0] for entries in firing.values() for entry in entries}
    used |= {death[0] for death in deaths}
    used |= {row[2] for row in multi if row[2] is not None}
    missing = sorted(name for name in used if name not in clips)
    if missing:
        raise TableError(f"animation {missing[0]} has no clip")

    out = banner("the GoldenEye decomp's src/game/chr.c, src/game/bondview.c and assets/animationtable_data.c")
    out += ['#include "extract/DecompTables.h"', "", "namespace Frigate::Extract::Detail {", ""]

    out.append("// {list, index, animation, flip, end frame, speed, knockback, thud frame 1, thud frame 2}")
    out.append("const StruckAnim kStruckAnims[] = {")
    for name, entries in struck.items():
        for index, e in enumerate(entries):
            out.append(f"    {{{cstr(name)}, {index}, {cstr(e[0])}, {e[1]}, {num(e[2])}, {num(e[3])}, {e[4]}, "
                       f"{num(e[5])}, {num(e[6])}}},")
    out += ["};", "const size_t kStruckAnimCount = sizeof(kStruckAnims) / sizeof(kStruckAnims[0]);", ""]

    out.append("// g_HitReactionTable: {hit part, impact puffs, impact size, back puffs, back size, death list,")
    out.append("//  flinch list}")
    out.append("const HitReaction kHitReactions[] = {")
    for r in reactions:
        out.append(f"    {{{r[0]}, {r[1]}, {num(r[2])}, {r[3]}, {num(r[4])}, {cstr_or_null(r[5])}, "
                   f"{cstr_or_null(r[6])}}},")
    out += ["};", "const size_t kHitReactionCount = sizeof(kHitReactions) / sizeof(kHitReactions[0]);", ""]

    out.append("// {table, index, animation, then the entry's 17 numbers in the decomp's field order}")
    out.append("const FiringAnim kFiringAnims[] = {")
    for name, entries in firing.items():
        for index, e in enumerate(entries):
            out.append(f"    {{{cstr(name)}, {index}, {cstr(e[0])},")
            out.append(f"     {{{', '.join(num(v) for v in e[1:])}}}}},")
    out += ["};", "const size_t kFiringAnimCount = sizeof(kFiringAnims) / sizeof(kFiringAnims[0]);", ""]

    out.append("// {set, group (a 1/32 turn), firing table}")
    out.append("const FiringGroup kFiringGroups[] = {")
    for name, tables in groups.items():
        for index, table in enumerate(tables):
            out.append(f"    {{{cstr(name)}, {index}, {cstr(table)}}},")
    out += ["};", "const size_t kFiringGroupCount = sizeof(kFiringGroups) / sizeof(kFiringGroups[0]);", ""]

    out.append("// {animation, flip, speed, start frame, unused frame, thud frame, end frame}")
    out.append("const ExplosionDeath kExplosionDeaths[] = {")
    for d in deaths:
        out.append(f"    {{{cstr(d[0])}, {d[1]}, {{{', '.join(num(v) for v in d[2:])}}}}},")
    out += ["};", "const size_t kExplosionDeathCount = sizeof(kExplosionDeaths) / sizeof(kExplosionDeaths[0]);", ""]

    out.append("// For each direction, {count, the explosion deaths it picks from}")
    out.append("const ExplosionGroup kExplosionGroups[] = {")
    for group in explosion_groups:
        out.append(f"    {{{len(group)}, {{{', '.join(str(v) for v in group)}}}}},")
    out += ["};", "const size_t kExplosionGroupCount = sizeof(kExplosionGroups) / sizeof(kExplosionGroups[0]);", ""]

    out.append("// firing_animation_groups, 6 stances to a group: {firing table or null, index, animation or null, x, y, z}")
    out.append("const MultiFiring kMultiFiring[] = {")
    for row in multi:
        out.append(f"    {{{cstr_or_null(row[0])}, {row[1]}, {_anim_ref(row[2])}, {num(row[3])}, {num(row[4])}, "
                   f"{num(row[5])}}},")
    out += ["};", "const size_t kMultiFiringCount = sizeof(kMultiFiring) / sizeof(kMultiFiring[0]);", ""]
    out += ["}  // namespace Frigate::Extract::Detail"] + footer()
    return Output("src/extract/ChrCombatData.cpp", "\n".join(out))


_ARRAY_RE = re.compile(r"(animation_table_ptrs[12])\s*\[\s*\]\s*=\s*\{(.*?)\};", re.DOTALL)
_ENTRY_RE = re.compile(r"PTR_ANIM_(\w+)|(\b0\b)")
_NULL_NAME = re.compile(r"^null\d+$")


def parse_animation_tables(source: str) -> Dict[int, List[Optional[str]]]:
    """Table number (1 or 2) -> the clip name of each id, None for an id with no clip (the `null<N>` placeholders)."""
    tables: Dict[int, List[Optional[str]]] = {}
    for match in _ARRAY_RE.finditer(source):
        number = int(match.group(1)[-1])
        names: List[Optional[str]] = []
        for entry in _ENTRY_RE.finditer(match.group(2)):
            if entry.group(2) is not None:
                break
            name = entry.group(1)
            names.append(None if _NULL_NAME.match(name) else name)
        tables[number] = names
    if sorted(tables) != [1, 2] or not tables[1] or not tables[2]:
        raise TableError("initanitable.c does not define both animation tables")
    return tables


def generate_animation_table(decomp: Decomp) -> Output:
    tables = parse_animation_tables(decomp.read("src/game/initanitable.c"))
    clips = {match.group(1) for match in _ANIM_DATA_RE.finditer(decomp.read("assets/animationtable_data.c"))}
    out = banner("the GoldenEye decomp's src/game/initanitable.c")
    out += ['#include "extract/DecompTables.h"', "", "namespace Frigate::Extract::Detail {", ""]
    out.append("// animation_table_ptrs1 and 2: {table, animation id, clip name or null for an id with no clip}")
    out.append("const AnimationId kAnimationIds[] = {")
    for number in (1, 2):
        for index, name in enumerate(tables[number]):
            if name is not None and name not in clips:
                raise TableError(f"animation {name} has no clip")
            out.append(f"    {{{number}, {index}, {cstr_or_null(name)}}},")
    out += ["};", "const size_t kAnimationIdCount = sizeof(kAnimationIds) / sizeof(kAnimationIds[0]);", ""]
    out += ["}  // namespace Frigate::Extract::Detail"] + footer()
    return Output("src/extract/AnimationTableData.cpp", "\n".join(out))


def generate_animation_clips(decomp: Decomp) -> Output:
    """Where each clip's header sits in the animation data file. The header words and the clips' bitstreams are
    ROM bytes, so only the layout of `animationtable_data.c` (its arrays, in file order) is needed."""
    text = decomp.read("assets/animationtable_data.c")
    out = banner("the GoldenEye decomp's assets/animationtable_data.c")
    out += ['#include "extract/DecompTables.h"', "", "namespace Frigate::Extract::Detail {", ""]
    out.append("// One row per ANIM_DATA_<name> array, in file order: {clip name, byte offset of its header in the")
    out.append("// animationtable_data file}. The file is the arrays' words concatenated.")
    out.append("const AnimationClipRow kAnimationClips[] = {")
    at = 0
    for match in _ANIM_DATA_RE.finditer(text):
        words = [token for token in (t.strip() for t in match.group(2).split(",")) if token]
        if len(words) < 5:
            raise TableError(f"ANIM_DATA_{match.group(1)} has {len(words)} words, not a clip header")
        out.append(f"    {{{cstr(match.group(1))}, 0x{at:05x}}},")
        at += 4 * len(words)
    out += ["};", "const size_t kAnimationClipCount = sizeof(kAnimationClips) / sizeof(kAnimationClips[0]);", ""]
    out += ["}  // namespace Frigate::Extract::Detail"] + footer()
    return Output("src/extract/AnimationClipData.cpp", "\n".join(out))
