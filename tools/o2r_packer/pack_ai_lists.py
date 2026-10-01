#!/usr/bin/env python3
"""Packs the global AI lists: the scripts every stage can hand to a character
(`g_GlobalAILists` in the GoldenEye decomp's chraidata.c), as the bytes the
game runs.

The decomp writes the lists with macros that expand differently under the
original compiler than under a host one, so this reads them from the ROM's
compressed data segment instead, where `g_GlobalAILists` is a table of
(list address, list id) pairs. Layout: docs/formats/goldeneye-ai-lists.md.
"""

from __future__ import annotations

import struct
from pathlib import Path
from typing import Dict, List, Tuple, Union

from ge_texture import rzip_inflate
from o2r_writer import O2rWriter

RESOURCE_PATH = "ai/globals"

# The US ROM's compressed data segment (scripts/extract_cdata.sh): where it
# starts, how long it is, and the address the game loads it at.
US_CDATA_ROM_OFFSET = 137616
US_CDATA_ROM_SIZE = 71760
US_CDATA_LOAD_ADDRESS = 0x80020D90
# g_GlobalAILists: {list address, list id} pairs ended by a null address. The
# lists are laid out back to back ahead of it.
US_TABLE_ADDRESS = 0x8003744C
# GAILIST_AIM_AT_BOND, the first list in the segment; the lists run from here
# to the table.
US_FIRST_LIST_ADDRESS = 0x80037070
# Enough for the table's 18 entries and its terminator; a longer table means
# the address above is wrong.
_MAX_ENTRIES = 64


class AiListsError(ValueError):
    """Raised when the data segment does not hold the table where expected."""


def inflate_cdata(rom: bytes) -> bytes:
    """The US ROM's inflated compressed data segment."""
    end = US_CDATA_ROM_OFFSET + US_CDATA_ROM_SIZE
    if len(rom) < end:
        raise AiListsError("the ROM is too short to hold the compressed data segment")
    return rzip_inflate(rom[US_CDATA_ROM_OFFSET:end])


def read_global_lists(cdata: bytes) -> Dict[int, bytes]:
    """List id -> the list's bytes, from the inflated data segment. A list
    runs to the start of the next one in memory (it may carry alignment
    padding after its end-of-list command, which is never run)."""
    def at(address: int) -> int:
        offset = address - US_CDATA_LOAD_ADDRESS
        if not 0 <= offset < len(cdata):
            raise AiListsError(f"address {address:#x} is outside the data segment")
        return offset

    entries: List[Tuple[int, int]] = []
    cursor = at(US_TABLE_ADDRESS)
    while True:
        if len(entries) >= _MAX_ENTRIES or cursor + 8 > len(cdata):
            raise AiListsError("g_GlobalAILists is not where expected")
        address, list_id = struct.unpack_from(">II", cdata, cursor)
        if address == 0:
            break
        entries.append((address, list_id))
        cursor += 8
    if not entries:
        raise AiListsError("g_GlobalAILists is empty")
    if sorted(list_id for _, list_id in entries) != list(range(len(entries))):
        raise AiListsError("g_GlobalAILists does not number its lists from 0")
    if min(address for address, _ in entries) != US_FIRST_LIST_ADDRESS:
        raise AiListsError("the first global AI list is not where expected")

    bounds = sorted(address for address, _ in entries) + [US_TABLE_ADDRESS]
    lists: Dict[int, bytes] = {}
    for address, list_id in entries:
        following = min(bound for bound in bounds if bound > address)
        lists[list_id] = bytes(cdata[at(address) : at(following)])
    return lists


def build_global_lists(rom: bytes) -> bytes:
    """The resource's bytes: one line per list, `<id>\\t<hex bytes>`."""
    lists = read_global_lists(inflate_cdata(rom))
    lines = [f"{list_id}\t{lists[list_id].hex()}" for list_id in sorted(lists)]
    return ("\n".join(lines) + "\n").encode("ascii")


def pack_ai_lists(rom_path: Union[str, Path], writer: O2rWriter) -> str:
    rom = Path(rom_path).read_bytes()
    writer.add_resource(RESOURCE_PATH, "OBLB", version=0, resource_id=0, payload=build_global_lists(rom))
    return RESOURCE_PATH
