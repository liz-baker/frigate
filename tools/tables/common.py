"""Shared helpers for the table generators: reading the decomp for one region, parsing C enums and initializers, and
writing the generated C++ files."""

from __future__ import annotations

import re
import textwrap
from pathlib import Path
from typing import Dict, List, NamedTuple, Optional, Tuple

# The preprocessor symbols each release's build defines (LCDEFS in the decomp's Makefile).
REGION_DEFINES = {
    "us": {"VERSION_US", "LANG_US", "REFRESH_NTSC", "BUGFIX_R0"},
    "eur": {"VERSION_EU", "LANG_EU", "REFRESH_PAL", "BUGFIX_R1", "BUGFIX_R2"},
    "jp": {"VERSION_JP", "LANG_JP", "REFRESH_NTSC", "BUGFIX_R1"},
}
REGIONS = tuple(REGION_DEFINES)
# The C++ enumerator of each region (extract/Region.h).
REGION_ENUMERATORS = {"us": "Us", "eur": "Eur", "jp": "Jp"}


class TableError(ValueError):
    """Raised when the decomp's sources don't have the shape the generators expect."""


_DIRECTIVE_RE = re.compile(r"^[ \t]*#[ \t]*(ifdef|ifndef|if|elif|else|endif)\b(.*)$")


def _evaluate(expression: str, defines) -> bool:
    expression = re.sub(r"/\*.*?\*/", "", expression)
    expression = re.sub(r"//.*", "", expression)
    expression = re.sub(r"defined\s*\(\s*(\w+)\s*\)|defined\s+(\w+)",
                        lambda m: "1" if (m.group(1) or m.group(2)) in defines else "0", expression)
    expression = re.sub(r"\b[A-Za-z_]\w*\b", lambda m: "1" if m.group(0) in defines else "0", expression)
    expression = expression.replace("&&", " and ").replace("||", " or ")
    expression = re.sub(r"!(?!=)", " not ", expression)
    if not re.fullmatch(r"[\s\d()andornt]*", expression):
        raise TableError(f"cannot evaluate the preprocessor condition `{expression}`")
    return bool(eval(expression, {"__builtins__": {}}))  # noqa: S307 - digits, parentheses, and/or/not only


def preprocess(text: str, defines) -> str:
    """Resolves the #if/#ifdef/#ifndef/#elif/#else/#endif lines of `text` for a build defining `defines`: the
    conditional lines and the lines of untaken branches become empty lines, everything else is kept."""
    out: List[str] = []
    # One entry per open conditional: [this branch is live, some branch was taken, the enclosing text is live].
    stack: List[List[bool]] = []

    def live() -> bool:
        return all(entry[0] and entry[2] for entry in stack)

    for line in text.split("\n"):
        match = _DIRECTIVE_RE.match(line)
        if match is None:
            out.append(line if live() else "")
            continue
        kind, rest = match.groups()
        if kind in ("if", "ifdef", "ifndef"):
            outer = live()
            if kind == "if":
                taken = outer and _evaluate(rest, defines)
            else:
                taken = (rest.strip().split()[0] in defines) == (kind == "ifdef")
            stack.append([taken, taken, outer])
        elif kind == "elif":
            entry = stack[-1]
            entry[0] = (not entry[1]) and entry[2] and _evaluate(rest, defines)
            entry[1] = entry[1] or entry[0]
        elif kind == "else":
            entry = stack[-1]
            entry[0] = not entry[1]
            entry[1] = True
        else:
            stack.pop()
        out.append("")
    if stack:
        raise TableError("unterminated preprocessor conditional")
    return "\n".join(out)


class Decomp:
    """A decomp checkout read for one region: every file comes back with that region's preprocessor conditionals
    resolved."""

    def __init__(self, root: Path, region: str):
        self.root = Path(root)
        self.region = region
        self.defines = REGION_DEFINES[region]

    def read(self, relative: str) -> str:
        return preprocess((self.root / relative).read_text(), self.defines)

    def path(self, relative: str) -> Path:
        return self.root / relative


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def enum_body(text: str, name: str) -> str:
    match = re.search(r"typedef\s+enum\s+" + name + r"\s*\{(.*?)\}\s*" + name + r"\s*;", text, re.S)
    if match is None:
        raise TableError(f"no `{name}` enum")
    return strip_comments(match.group(1))


def enum_entries(text: str, name: str) -> List[Tuple[str, int]]:
    """(name, value) of each enumerator of `typedef enum <name> {...} <name>;`: a value is the explicit number, an
    earlier enumerator's value, or the previous value plus one."""
    known: Dict[str, int] = {}
    entries: List[Tuple[str, int]] = []
    value = 0
    for item in enum_body(text, name).split(","):
        item = item.strip()
        if not item:
            continue
        label, _, assigned = (part.strip() for part in item.partition("="))
        if assigned:
            value = known[assigned] if assigned in known else int(assigned, 0)
        known[label] = value
        entries.append((label, value))
        value += 1
    return entries


def balanced(text: str, open_at: int, opener: str = "{", closer: str = "}") -> Tuple[str, int]:
    """The text between the bracket at `open_at` and its match, and the index after the match."""
    depth = 0
    for index in range(open_at, len(text)):
        if text[index] == opener:
            depth += 1
        elif text[index] == closer:
            depth -= 1
            if depth == 0:
                return text[open_at + 1 : index], index + 1
    raise TableError("unbalanced brackets")


def split_top_level(text: str) -> List[str]:
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


def cstr(text: str) -> str:
    """A C++ string literal."""
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def cstr_or_null(text: Optional[str]) -> str:
    return "nullptr" if text is None else cstr(text)


def num(value: float) -> str:
    """A C++ double literal that reads back as `value`."""
    return repr(float(value))


class Output(NamedTuple):
    path: str  # relative to the repository root
    text: str


def banner(source: str) -> List[str]:
    """The top of a generated C++ file."""
    text = f"Generated by tools/gen_tables.py from {source}. Do not edit by hand; regenerate with `just gen-tables`."
    return [f"// {line}" for line in textwrap.wrap(text, 116)] + ["", "// clang-format off", ""]


def footer() -> List[str]:
    return ["// clang-format on", ""]
