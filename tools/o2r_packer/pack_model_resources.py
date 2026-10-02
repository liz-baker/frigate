#!/usr/bin/env python3
"""Writes the vertex, display-list and embedded-image resources of
GoldenEye's model files and bg rooms into an O2R archive (frigate issue
#18). Paths and payloads: docs/formats/goldeneye-display-list-resource.md
and docs/formats/goldeneye-vertex.md.

A model's node tree is written as a `GEMD` resource (ge_modeltree.py). The
raw model and bg files still stay in the archive as blobs (pack_objects.py,
pack_geometry.py); dropping them is
https://github.com/liz-baker/frigate/issues/56.
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable, Dict, List, Sequence, Union

from ge_animation import (
    ANIMATION_CLIP_RESOURCE_TYPE,
    ANIMATION_CLIP_RESOURCE_VERSION,
    encode_animation_clip_payload,
    parse_animations,
)
from ge_container import GeContainerError, ModelImage, bg_header_region, unwrap_bg_file
from ge_dlemit import (
    DISPLAY_LIST_RESOURCE_TYPE,
    DISPLAY_LIST_RESOURCE_VERSION,
    TextureBinding,
    ReemittedLists,
    bg_header_path,
    bg_room_base_path,
    binding_for_format,
    encode_embedded_image_payload,
    model_base_path,
    reemit_bg_room_lists,
    reemit_effect_images,
    reemit_model_lists,
)
from ge_model import parse_model_tree, read_model_header_counts
from ge_modeltree import MODEL_TREE_RESOURCE_TYPE, MODEL_TREE_RESOURCE_VERSION, encode_model_tree_payload
from ge_skeleton import SKELETON_RESOURCE_TYPE, SKELETON_RESOURCE_VERSION, encode_skeleton_payload, parse_skeletons
from ge_texture import decode_texture
from ge_vertex import (
    VERTEX_RESOURCE_TYPE,
    VERTEX_RESOURCE_VERSION,
    BgVertexKey,
    VertexBlock,
    encode_vertex_payload,
    extract_bg_vertices,
    extract_model_vertices,
)
from o2r_writer import O2rWriter
from pack_objects import parse_filelist_obseg
from pack_textures import ImageEntry

TEXTURE_PREFIX = "textures"
_IMAGE_RESOURCE_TYPE = "OTEX"
_IMAGE_RESOURCE_VERSION = 1
_BLOB_RESOURCE_TYPE = "OBLB"
_BLOB_RESOURCE_VERSION = 0
_MODEL_CATEGORIES = ("chr", "gun", "prop")


def make_texture_lookup(images: Sequence[ImageEntry], split_dir: Union[str, Path]) -> Callable[[int], TextureBinding]:
    """Maps a `G_SETTEX` texture id to the resource pack_textures.py wrote
    for it, decoding each image once to learn its format and width."""
    split_dir = Path(split_dir)
    bindings: Dict[int, TextureBinding] = {}

    def lookup(texture_id: int) -> TextureBinding:
        if texture_id not in bindings:
            name = images[texture_id].name
            decoded = decode_texture((split_dir / f"{name}.bin").read_bytes())
            path = f"{TEXTURE_PREFIX}/{name}"
            palette = f"{path}.pal" if decoded.palette_rgba5551 is not None else None
            bindings[texture_id] = binding_for_format(path, decoded.format, decoded.width, palette)
        return bindings[texture_id]

    return lookup


def _write(writer: O2rWriter, path: str, resource_type: str, version: int, payload: bytes) -> None:
    writer.add_resource(path, resource_type, version=version, resource_id=0, payload=payload)


def _write_vertex_blocks(writer: O2rWriter, base: str, blocks: List[VertexBlock]) -> None:
    for block in blocks:
        key = block.key.offset if isinstance(block.key, BgVertexKey) else block.key.node
        _write(writer, f"{base}/vtx/{key:06x}", VERTEX_RESOURCE_TYPE, VERTEX_RESOURCE_VERSION, encode_vertex_payload(block.vertices))


def _write_lists(writer: O2rWriter, result: ReemittedLists) -> None:
    for emitted in result.lists.values():
        _write(writer, emitted.path, DISPLAY_LIST_RESOURCE_TYPE, DISPLAY_LIST_RESOURCE_VERSION, emitted.payload)
    for image in result.images.values():
        _write(writer, image.path, _IMAGE_RESOURCE_TYPE, _IMAGE_RESOURCE_VERSION, encode_embedded_image_payload(image))


def pack_model_resources(
    filelist_csv_path: Union[str, Path],
    decomp_dir: Union[str, Path],
    writer: O2rWriter,
    textures: Callable[[int], TextureBinding],
    *,
    categories: Sequence[str] = _MODEL_CATEGORIES,
) -> List[str]:
    """Every chr/gun/prop model file in the filelist, as vertex blocks,
    lists and embedded images. Returns the files missing after extraction."""
    decomp_dir = Path(decomp_dir)
    counts = read_model_header_counts(decomp_dir)
    skeletons = parse_skeletons(decomp_dir)
    missing: List[str] = []
    for entry in parse_filelist_obseg(filelist_csv_path, categories):
        path = decomp_dir / entry.relative_path
        if not path.exists():
            missing.append(entry.relative_path)
            continue
        # The decomp's extractor has already inflated model files.
        image = ModelImage(path.read_bytes())
        model_counts = counts[entry.name[1:-1]]
        tree = parse_model_tree(image, model_counts)
        blocks = extract_model_vertices(image, tree)
        base = model_base_path(entry.relative_path)
        _write_vertex_blocks(writer, base, blocks)
        _write(writer, f"{base}/tree", MODEL_TREE_RESOURCE_TYPE, MODEL_TREE_RESOURCE_VERSION, encode_model_tree_payload(image, tree, textures, model_counts.radius))
        _write_lists(writer, reemit_model_lists(base, image, tree, blocks, textures))
        _write_lists(writer, reemit_effect_images(base, image, tree, textures))
        if model_counts.skeleton_name is not None:
            _write(
                writer,
                f"{base}/skeleton",
                SKELETON_RESOURCE_TYPE,
                SKELETON_RESOURCE_VERSION,
                encode_skeleton_payload(skeletons[model_counts.skeleton_name]),
            )
    return missing


def pack_animation_resources(decomp_dir: Union[str, Path], writer: O2rWriter) -> None:
    """Every animation clip, as a top-level `animations/<name>` resource --
    shared across models via a joint's channel index (#70), not scoped to
    any one model's own path. Requires `assets/animationtable_entries.bin`
    to already be extracted from a ROM (ge_animation.parse_animations)."""
    decomp_dir = Path(decomp_dir)
    for name, clip in parse_animations(decomp_dir).items():
        _write(
            writer,
            f"animations/{name}",
            ANIMATION_CLIP_RESOURCE_TYPE,
            ANIMATION_CLIP_RESOURCE_VERSION,
            encode_animation_clip_payload(clip),
        )


def pack_bg_resources(
    bg_files: Sequence[str],
    decomp_dir: Union[str, Path],
    writer: O2rWriter,
    textures: Callable[[int], TextureBinding],
) -> List[str]:
    """Every room of each `bg_*_all_p.bin` in `bg_files` (paths relative to
    the decomp), plus the file's header region as a `bg/<stem>/header`
    blob. Files with no rooms are skipped; returns the missing ones."""
    decomp_dir = Path(decomp_dir)
    missing: List[str] = []
    for name in bg_files:
        path = decomp_dir / name
        if not path.exists():
            missing.append(name)
            continue
        data = path.read_bytes()
        try:
            rooms = unwrap_bg_file(data)
            header = bg_header_region(data)
        except GeContainerError:
            continue  # an empty placeholder file
        writer.add_resource(bg_header_path(name), _BLOB_RESOURCE_TYPE, version=_BLOB_RESOURCE_VERSION, resource_id=0, payload=header)
        for room in rooms:
            blocks = extract_bg_vertices(room)
            base = bg_room_base_path(name, room.index)
            _write_vertex_blocks(writer, base, blocks)
            _write_lists(writer, reemit_bg_room_lists(base, room, blocks, textures))
    return missing
