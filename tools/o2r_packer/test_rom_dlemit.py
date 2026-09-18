#!/usr/bin/env python3
"""Opt-in check of display-list re-emission against your own US ROM.
Skipped unless `FRIGATE_ROMS` points at it (see rom_regions.py).

    FRIGATE_ROMS=/path/to/roms python3 -m unittest test_rom_dlemit -v

Runs every chr/gun/prop model file and every non-empty bg file through the
re-emitter and checks that each reference in the emitted lists resolves to
a resource that is emitted (or, for textures, named in `images.def`), and
that the emitted commands account for every command the walker counts.
"""

import struct
import unittest
from pathlib import Path

from decomp_extract import run_extraction_mode
from ge_container import GeContainerError, unwrap_bg_file, unwrap_model_file
from ge_displaylist import G_DL, G_ENDDL, G_SETTIMG, G_VTX, walk_display_list
from ge_dlemit import (
    G_SETTEX,
    OTR_G_DL_HASH,
    OTR_G_SETTIMG_HASH,
    OTR_G_VTX_HASH,
    READER_END_OPCODE,
    bg_room_base_path,
    binding_for_format,
    iter_commands,
    model_base_path,
    path_hash,
    reemit_bg_room_lists,
    reemit_model_lists,
)
from ge_model import parse_model_tree, read_model_header_counts
from ge_texture import decode_texture
from ge_vertex import extract_bg_vertices, extract_model_vertices
from pack_textures import parse_images_def
from rom_regions import REGIONS, ROMS_ENV_VAR, discover_roms, locations_from_env
from test_rom_containers import FILELIST, _file_bytes

DECOMP_DIR = Path(__file__).resolve().parents[2] / "external" / "007"
TEXTURE_PREFIX = "textures"
# Rewritten commands; every other opcode must pass through in equal number.
REWRITTEN = {G_VTX, G_DL, G_SETTEX, G_SETTIMG, G_ENDDL}


class RomDisplayListEmitTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.roms = discover_roms(locations_from_env(), DECOMP_DIR)

    def setUp(self):
        if "u" not in self.roms:
            self.skipTest(f"set {ROMS_ENV_VAR} to your own US ROM file or directory to run this")
        self.rom = self.roms["u"]
        images_def = DECOMP_DIR / "assets" / "images.def"
        split = DECOMP_DIR / "assets" / "images" / "split"
        self.images = parse_images_def(images_def)
        if not (split / f"{self.images[-1].name}.bin").exists():
            run_extraction_mode(DECOMP_DIR, self.rom, REGIONS["u"].image_extraction_mode)
        self.split = split
        names = {f"{TEXTURE_PREFIX}/{e.name}" for e in self.images}
        self.texture_paths = names | {f"{p}.pal" for p in names}
        self.texture_by_hash = {path_hash(p): p for p in self.texture_paths}
        self.bindings = {}
        self.texture_ids_seen = set()

    def lookup(self, texture_id):
        self.texture_ids_seen.add(texture_id)
        if texture_id not in self.bindings:
            name = self.images[texture_id].name
            decoded = decode_texture((self.split / f"{name}.bin").read_bytes())
            path = f"{TEXTURE_PREFIX}/{name}"
            palette = f"{path}.pal" if decoded.palette_rgba5551 is not None else None
            self.bindings[texture_id] = binding_for_format(path, decoded.format, decoded.width, palette)
        return self.bindings[texture_id]

    def _assert_resolves(self, result, vertex_paths, where):
        """Every reference, and every hash in every payload, names a
        resource that exists; the two agree."""
        emitted_paths = set(result.lists) | set(result.images) | set(vertex_paths)
        known = emitted_paths | self.texture_paths
        by_hash = dict(self.texture_by_hash)
        by_hash.update((path_hash(path), path) for path in emitted_paths)
        self.assertEqual(len(by_hash), len(known), f"{where}: path hash collision")
        for emitted in result.lists.values():
            hashed = []
            for opcode, _, _, digest in iter_commands(emitted.payload):
                if digest is not None:
                    self.assertIn(digest, by_hash, f"{where} {emitted.path}")
                    hashed.append((opcode, by_hash[digest]))
            self.assertEqual(
                sorted(path for _, path in hashed),
                sorted(r.path for r in emitted.references if r.kind not in ("palette", "detail")),
                f"{where} {emitted.path}",
            )
            for ref in emitted.references:
                self.assertIn(ref.path, known, f"{where} {emitted.path}")
                if ref.kind == "vertex":
                    self.assertIn(ref.path, vertex_paths)
                elif ref.kind == "list":
                    self.assertIn(ref.path, result.lists)
                elif ref.kind == "image":
                    self.assertIn(ref.path, result.images)
            self.assertEqual(emitted.payload[-8:-4], struct.pack(">I", READER_END_OPCODE << 24))

    def _assert_counts(self, result, walks, where):
        """`walks` are the walker's results for each root list emitted."""
        self.assertEqual(
            sum(e.source_commands for e in result.lists.values()), sum(w.command_count for w in walks), where
        )
        emitted = {}
        for e in result.lists.values():
            for opcode, *_ in iter_commands(e.payload):
                emitted[opcode] = emitted.get(opcode, 0) + 1
        source = {}
        for w in walks:
            for opcode, n in w.opcode_counts.items():
                source[opcode] = source.get(opcode, 0) + n
        self.assertEqual(emitted.get(OTR_G_VTX_HASH, 0), source.get(G_VTX, 0), where)
        self.assertEqual(emitted.get(OTR_G_DL_HASH, 0), source.get(G_DL, 0), where)
        self.assertEqual(
            emitted.get(OTR_G_SETTIMG_HASH, 0), source.get(G_SETTEX, 0) + source.get(G_SETTIMG, 0), where
        )
        for opcode, n in source.items():
            if opcode not in REWRITTEN:
                self.assertEqual(emitted.get(opcode, 0), n, f"{where}: opcode {opcode:#x}")

    def test_every_model_file_reemits_with_resolving_references(self):
        counts = read_model_header_counts(DECOMP_DIR)
        names = [
            line.split(",")[2]
            for line in FILELIST.read_text().splitlines()
            if any(f"/obseg/{kind}/" in line for kind in ("chr", "gun", "prop"))
        ]
        self.assertEqual(len(names), 512)
        lists = images = settex_files = 0
        for name in names:
            with self.subTest(name):
                image = unwrap_model_file(_file_bytes(self.rom, name))
                tree = parse_model_tree(image, counts[name.rsplit("/", 1)[1][1:-len("Z.bin")]])
                blocks = extract_model_vertices(image, tree)
                base = model_base_path(name)
                self.texture_ids_seen = set()
                result = reemit_model_lists(base, image, tree, blocks, self.lookup)

                self.assertLessEqual(self.texture_ids_seen, {t.texture_id for t in tree.textures}, name)
                walks = []
                for offset, node in tree.nodes.items():
                    record = node.display_list
                    if record is None:
                        continue
                    resolve = image.display_list_resolver(record.vertices_address)
                    visited = [
                        walk_display_list(image.data, s, resolve)
                        for s in (record.primary, record.secondary)
                        if s is not None
                    ]
                    if len(visited) == 2:
                        # Shared lists would be counted twice below.
                        self.assertFalse(visited[0].visited_lists & visited[1].visited_lists, name)
                    walks.extend(visited)
                    for kind, start in (("primary", record.primary), ("secondary", record.secondary)):
                        if start is not None:
                            self.assertIn((offset, kind), result.roots, name)
                self._assert_resolves(result, {f"{base}/vtx/{b.key.node:06x}" for b in blocks}, name)
                self._assert_counts(result, walks, name)
                lists += len(result.lists)
                images += len(result.images)
                settex_files += bool(self.texture_ids_seen)
        self.assertGreater(lists, 2000)
        # The three props that draw their own pixel data (logos and the legal page).
        self.assertEqual(images, 8)
        self.assertGreater(settex_files, 100)

    def test_every_bg_file_reemits_with_resolving_references(self):
        names = [
            line.split(",")[2]
            for line in FILELIST.read_text().splitlines()
            if "/obseg/bg/bg_" in line and line.split(",")[2].endswith("_all_p.bin")
        ]
        checked = 0
        for name in names:
            try:
                rooms = unwrap_bg_file(_file_bytes(self.rom, name))
            except GeContainerError:
                continue  # an empty placeholder file
            checked += 1
            with self.subTest(name):
                for room in rooms:
                    where = f"{name} room {room.index}"
                    blocks = extract_bg_vertices(room)
                    base = bg_room_base_path(name, room.index)
                    result = reemit_bg_room_lists(base, room, blocks, self.lookup)
                    self.assertEqual(result.images, {}, where)
                    walks = [
                        walk_display_list(blob, 0, room.resolve)
                        for blob in (room.primary, room.secondary)
                        if blob is not None
                    ]
                    self.assertEqual(
                        sorted(kind for _, kind in result.roots), sorted(["primary"] + ["secondary"] * (room.secondary is not None))
                    )
                    self._assert_resolves(result, {f"{base}/vtx/{b.key.offset:06x}" for b in blocks}, where)
                    self._assert_counts(result, walks, where)
        self.assertEqual(checked, 25)


if __name__ == "__main__":
    unittest.main()
