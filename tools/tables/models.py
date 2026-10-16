"""Tables about the character, prop and gun models: the model header counts, the chr model records, the hat
placements and the prop model records."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Dict, List, NamedTuple, Optional, Tuple

from .common import Decomp, Output, TableError, banner, cstr, cstr_or_null, enum_body, enum_entries, footer, num

# MODELFILEHEADER(name, rootnode, skeleton, switches, numswitches, nummatrices, radius, numrecords, numtextures)
_HEADER_MACRO = re.compile(r"^\s*MODELFILEHEADER\(\s*(\w+)\s*,([^\n]*)\)\s*$", re.M)
_HEADER_ARG_COUNT = 8
_HEADER_SKELETON_ARG = 1
_HEADER_NUM_SWITCHES_ARG = 3
_HEADER_RADIUS_ARG = 5
_HEADER_NUM_TEXTURES_ARG = 7
_SKELETON_ARG_RE = re.compile(r"SKELETON\(\s*(\w+)\s*\)")

# BODIES and HEADS markers that are not slots of their own.
_ENUM_MARKERS = {"HEAD_START", "HEAD_F_START", "HEAD_BOND_START", "HEAD_END", "BODIES_MAX"}
_RECORD_RE = re.compile(
    r'\{\s*&\w+\s*,\s*"(?P<file>\w+)"\s*,\s*(?P<scale>[-\d.e]+)f?\s*,\s*(?P<pov>[-\d.e]+)f?\s*,'
    r"\s*(?P<male>0x[0-9a-fA-F]+|\d+)\s*,\s*(?P<head>0x[0-9a-fA-F]+|\d+)\s*,"
)
_CHR_INCLUDE_RE = re.compile(r"#include\s*<(assets/obseg/chr/[^>]+)>")
HATS_PER_HEAD = 6
_HAT_FLOAT = r"(-?[\d.]+(?:e-?\d+)?)"
_HAT_RE = re.compile(r"\{\s*" + r"\s*,\s*".join([_HAT_FLOAT] * 6) + r"\s*,?\s*\}")

_DEFAULT_PROP_SCALE = "0.1"
_PROP_INCLUDE_RE = re.compile(r"#include\s*<(assets/obseg/prop/[^>]+/propFileRecord\.inc\.c)>")
_PROP_RECORD_RE = re.compile(r"^\s*PROPFILERECORD\(\s*(\w+)\s*,\s*([^)]*?)\s*\)", re.M)
_EXPLOSION_RECORD_RE = re.compile(r"\{\s*(EXPLOSION_\w+)\s*,\s*\{([^}]*)\}")


class ModelHeader(NamedTuple):
    name: str
    num_switches: int
    num_textures: int
    skeleton: Optional[str]
    radius: float


def read_model_headers(decomp: Decomp) -> Dict[str, ModelHeader]:
    """Every `MODELFILEHEADER` of the chr, gun and prop tables, by header name (`camguard`, `ak47`)."""
    headers: Dict[str, ModelHeader] = {}
    for path in sorted((decomp.root / "assets" / "obseg").rglob("*odelFileHeader*.inc.c")):
        text = decomp.read(str(path.relative_to(decomp.root)))
        for match in _HEADER_MACRO.finditer(text):
            args = [arg.strip() for arg in match.group(2).split(",")]
            if len(args) != _HEADER_ARG_COUNT:
                raise TableError(f"{path.name}: {match.group(1)} has {len(args)} header arguments")
            skeleton = _SKELETON_ARG_RE.search(args[_HEADER_SKELETON_ARG])
            headers[match.group(1)] = ModelHeader(
                match.group(1),
                int(args[_HEADER_NUM_SWITCHES_ARG], 0),
                int(args[_HEADER_NUM_TEXTURES_ARG], 0),
                skeleton.group(1) if skeleton else None,
                float(args[_HEADER_RADIUS_ARG]),
            )
    return headers


_JOINT_ARRAY_RE = re.compile(r"ModelJoint\s+JOINTLIST\((\w+)\)\s*\[\s*\w*\s*\]\s*=\s*\{(.*?)\};", re.S)
_JOINT_ENTRY_RE = re.compile(
    r"\{\s*(0[xX][0-9a-fA-F]+|\d+)\s*,\s*(0[xX][0-9a-fA-F]+|\d+)\s*,\s*(0[xX][0-9a-fA-F]+|\d+)\s*\}")
_SKELETON_MACRO_RE = re.compile(r"MODELSKELETON\(\s*(\w+)\s*,\s*([^,]+),\s*[^)]+\)")


def read_skeletons(decomp: Decomp) -> Dict[str, List[Tuple[int, int, int]]]:
    """Every skeleton's joints as (node type, matrix a, matrix b), by the name a header's `SKELETON(name)` uses.
    Several skeletons often share one source file."""
    skeletons: Dict[str, List[Tuple[int, int, int]]] = {}
    counts: Dict[str, int] = {}
    for path in sorted((decomp.root / "assets" / "embedded" / "skeletons").glob("*.inc.c")):
        text = decomp.read(str(path.relative_to(decomp.root)))
        for match in _JOINT_ARRAY_RE.finditer(text):
            if match.group(1) in skeletons:
                raise TableError(f"{path.name}: skeleton {match.group(1)} is defined more than once")
            skeletons[match.group(1)] = [
                (int(a, 0), int(b, 0), int(c, 0)) for a, b, c in _JOINT_ENTRY_RE.findall(match.group(2))
            ]
        for match in _SKELETON_MACRO_RE.finditer(text):
            counts[match.group(1)] = int(match.group(2).strip(), 0)
    for name, joints in skeletons.items():
        if name in counts and counts[name] != len(joints):
            raise TableError(f"skeleton {name}: MODELSKELETON names {counts[name]} joints but JOINTLIST has "
                             f"{len(joints)}")
    return skeletons


def model_header_name(file: str) -> str:
    """The header name of a model file: `CcamguardZ` is `camguard`."""
    return file[1:-1]


def slot_names(bondconstants: str) -> Dict[int, List[str]]:
    """Chr slot index -> the BODY_* and HEAD_* names for it. `#ifdef ALL_BONDS` enumerators are not in the build."""
    slots: Dict[int, List[str]] = {}
    for enum in ("BODIES", "HEADS"):
        for name, value in _enumerators_until_end(bondconstants, enum):
            if name not in _ENUM_MARKERS:
                slots.setdefault(value, []).append(name)
    return slots


def _enumerators_until_end(bondconstants: str, enum: str) -> List[Tuple[str, int]]:
    """The enumerators of `enum` through HEAD_END or BODIES_MAX (what follows is counts and sentinels). A value that
    is another enumerator's name (`HEAD_START = BODY_...`) takes that value."""
    known = {}
    for other in ("BODIES", "HEADS"):
        if other == enum:
            break
        for name, value in enum_entries(bondconstants, other):
            known[name] = value
    out: List[Tuple[str, int]] = []
    next_value = 0
    for item in enum_body(bondconstants, enum).split(","):
        item = re.sub(r"#\w+.*", "", item).strip()
        if not item:
            continue
        if "=" in item:
            name, expression = (part.strip() for part in item.split("=", 1))
            value = known[expression] if expression in known else int(expression, 0)
        else:
            name, value = item, next_value
        known[name] = value
        out.append((name, value))
        next_value = value + 1
        if name in ("HEAD_END", "BODIES_MAX"):
            break
    return out


class ChrRecord(NamedTuple):
    names: str
    file: str
    scale: str
    pov: str
    is_male: int
    has_head: int


def read_chr_records(decomp: Decomp) -> List[ChrRecord]:
    slots = slot_names(decomp.read("src/bondconstants.h"))
    table = decomp.read("assets/obseg/chr/chrModelFileRecords.inc.c")
    records: List[ChrRecord] = []
    for index, include in enumerate(_CHR_INCLUDE_RE.findall(table)):
        match = _RECORD_RE.search(decomp.read(include))
        if match is None:
            raise TableError(f"{include} holds no chr model record")
        if index not in slots:
            raise TableError(f"chr slot {index} has no BODY_ or HEAD_ name")
        records.append(ChrRecord(",".join(slots[index]), match.group("file"), match.group("scale"),
                                 match.group("pov"), int(match.group("male"), 0), int(match.group("head"), 0)))
    if sorted(slots) != list(range(len(records))):
        raise TableError(f"the enums name slots {min(slots)}..{max(slots)} but there are {len(records)} records")
    return records


def read_hats(decomp: Decomp) -> List[Tuple[str, ...]]:
    """The raw text of each hat placement of `chrHeadHats.inc.c`: three offsets and three sizes."""
    text = re.sub(r"//[^\n]*", "", decomp.read("assets/obseg/chr/chrHeadHats.inc.c"))
    entries = _HAT_RE.findall(text[text.index("{") + 1 :])
    if not entries or len(entries) % HATS_PER_HEAD:
        raise TableError(f"chrHeadHats.inc.c holds {len(entries)} entries, not a multiple of {HATS_PER_HEAD}")
    return entries


class PropRecord(NamedTuple):
    name: str  # PROP_*
    model: str
    scale: str
    explosion: int
    seeds: List[int]


def read_prop_records(decomp: Decomp) -> List[PropRecord]:
    constants = decomp.read("src/bondconstants.h")
    match = re.search(r"typedef\s+enum\s+PROP\s*\{(.*?)\}\s*PROP\s*;", constants, re.S)
    if match is None:
        raise TableError("no `PROP` enum")
    body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.S)
    names = [item.split("=")[0].strip() for item in body.split(",")]
    names = [name for name in names if name and name != "PROP_MAX"]

    table = decomp.read("assets/obseg/prop/propItemModelFileRecord.inc.c")
    models: List[Tuple[str, str]] = []
    for include in _PROP_INCLUDE_RE.findall(table):
        found = _PROP_RECORD_RE.findall(decomp.read(include))
        if len(found) != 1:
            raise TableError(f"{include} holds {len(found)} records, expected 1")
        models.append((found[0][0], found[0][1] or _DEFAULT_PROP_SCALE))
    if len(models) != len(names):
        raise TableError(f"PROP names {len(names)} models but the table has {len(models)} records")

    # E_EXPLOSIONTYPE values are the enumerator positions.
    explosion_body = re.search(r"typedef\s+enum\s+E_EXPLOSIONTYPE\s*\{(.*?)\}\s*E_EXPLOSIONTYPE\s*;", constants, re.S)
    if explosion_body is None:
        raise TableError("no `E_EXPLOSIONTYPE` enum")
    body = re.sub(r"/\*.*?\*/", "", explosion_body.group(1), flags=re.S)
    kinds = {item.split("=")[0].strip(): index for index, item in enumerate(body.split(",")) if item.strip()}
    details = _EXPLOSION_RECORD_RE.findall(decomp.read("assets/obseg/prop/propExplosionDetailsRecords.inc.c"))
    # The last record ends the table.
    if len(details) != len(names) + 1:
        raise TableError(f"the explosion details have {len(details)} records, expected {len(names) + 1}")
    records = []
    for name, (model, scale), (kind, seeds) in zip(names, models, details):
        seed_values = [int(seed.strip(), 0) for seed in seeds.split(",") if seed.strip()]
        if len(seed_values) != 6:
            raise TableError(f"an explosion record names {len(seed_values)} seeds, expected 6")
        records.append(PropRecord(name, model, scale, kinds[kind], seed_values))
    return records


def generate_model_headers(decomp: Decomp) -> Output:
    headers = read_model_headers(decomp)
    out = banner("the GoldenEye decomp's assets/obseg/{chr,gun,prop} MODELFILEHEADER tables")
    out += ['#include "extract/DecompTables.h"', "", "namespace Frigate::Extract::Detail {", ""]
    out.append("// {name, switch count, texture count, skeleton, bounding radius}, sorted by name.")
    out.append("const ModelHeader kModelHeaders[] = {")
    for name in sorted(headers):
        h = headers[name]
        out.append(f"    {{{cstr(name)}, {h.num_switches}, {h.num_textures}, {cstr_or_null(h.skeleton)}, "
                   f"{num(h.radius)}}},")
    out += ["};", "const size_t kModelHeaderCount = sizeof(kModelHeaders) / sizeof(kModelHeaders[0]);", ""]
    out += ["}  // namespace Frigate::Extract::Detail"] + footer()
    return Output("src/extract/ModelHeaderData.cpp", "\n".join(out))


def generate_skeletons(decomp: Decomp) -> Output:
    skeletons = read_skeletons(decomp)
    out = banner("the GoldenEye decomp's assets/embedded/skeletons JOINTLIST arrays")
    out += ['#include "extract/DecompTables.h"', "", "namespace Frigate::Extract::Detail {", ""]
    out.append("// {node type, matrix a, matrix b} of every joint of every skeleton, in skeleton name order.")
    out.append("const SkeletonJoint kSkeletonJoints[] = {")
    entries = []
    at = 0
    for name in sorted(skeletons):
        entries.append((name, at, len(skeletons[name])))
        out.append(f"    // {name}")
        out += [f"    {{{kind}, {a}, {b}}}," for kind, a, b in skeletons[name]]
        at += len(skeletons[name])
    out += ["};", "", "// {skeleton name, first joint, joint count}, sorted by name.", "const SkeletonEntry kSkeletons[] = {"]
    out += [f"    {{{cstr(name)}, {first}, {count}}}," for name, first, count in entries]
    out += ["};", "const size_t kSkeletonCount = sizeof(kSkeletons) / sizeof(kSkeletons[0]);", ""]
    out += ["}  // namespace Frigate::Extract::Detail"] + footer()
    return Output("src/extract/SkeletonData.cpp", "\n".join(out))


def generate_chr_records(decomp: Decomp) -> Output:
    records = read_chr_records(decomp)
    hats = read_hats(decomp)
    out = banner("the GoldenEye decomp's BODIES and HEADS enums, chrModelFileRecords.inc.c and chrHeadHats.inc.c")
    out += ['#include "extract/DecompTables.h"', "", "namespace Frigate::Extract::Detail {", ""]
    out.append("// One row per c_item_entries slot: {BODY_/HEAD_ names, model file, isMale, hasHead, scale, root motion")
    out.append("// scale}. The scales are the decomp's own text. The array index is the slot.")
    out.append("const ChrRecord kChrRecords[] = {")
    for index, r in enumerate(records):
        out.append(f"    /* {index:3} */ {{{cstr(r.names)}, {cstr(r.file)}, {r.is_male}, {r.has_head}, {cstr(r.scale)}, "
                   f"{cstr(r.pov)}}},")
    out += ["};", "const size_t kChrRecordCount = sizeof(kChrRecords) / sizeof(kChrRecords[0]);", ""]
    out.append(f"// Hat placements, {HATS_PER_HEAD} per male head: {{x offset, y offset, z offset, x size, y size, z size}}")
    out.append("// as the decomp writes them.")
    out.append("const ChrHat kChrHats[] = {")
    for index, hat in enumerate(hats):
        out.append(f"    /* {index:3} */ {{{{{', '.join(cstr(v) for v in hat)}}}}},")
    out += ["};", "const size_t kChrHatCount = sizeof(kChrHats) / sizeof(kChrHats[0]);", ""]
    out += ["}  // namespace Frigate::Extract::Detail"] + footer()
    return Output("src/extract/ChrRecordData.cpp", "\n".join(out))


def generate_prop_models(decomp: Decomp) -> Output:
    records = read_prop_records(decomp)
    out = banner("the GoldenEye decomp's PROP and E_EXPLOSIONTYPE enums, propItemModelFileRecord.inc.c and "
                 "propExplosionDetailsRecords.inc.c")
    out += ['#include "extract/DecompTables.h"', "", "namespace Frigate::Extract::Detail {", ""]
    out.append("// One row per PROP value: {PROP_ name, model name (the file is P<model>Z), scale, explosion type, six")
    out.append("// deformation seeds}. The scale is the decomp's own text.")
    out.append("const PropRecord kPropRecords[] = {")
    for r in records:
        seeds = ", ".join(str(s) for s in r.seeds)
        out.append(f"    {{{cstr(r.name)}, {cstr(r.model)}, {cstr(r.scale)}, {r.explosion}, {{{seeds}}}}},")
    out += ["};", "const size_t kPropRecordCount = sizeof(kPropRecords) / sizeof(kPropRecords[0]);", ""]
    out += ["}  // namespace Frigate::Extract::Detail"] + footer()
    return Output("src/extract/PropRecordData.cpp", "\n".join(out))
