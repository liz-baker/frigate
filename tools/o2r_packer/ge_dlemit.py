#!/usr/bin/env python3
"""Re-emits GoldenEye display lists as libultraship `Fast::DisplayList`
resources whose vertex, child-list and texture commands name other
resources by path (frigate issue #18, display-list step). Resource-path
scheme and command encodings: docs/formats/goldeneye-display-list-resource.md.

libultraship stores a reference as a 128-bit command carrying the CRC64 of
the target's path, so nothing here writes a segment address. Every other
command is copied byte for byte: GoldenEye's opcodes are the ones the
libultraship `ucode_indy_ge` handler table already decodes.

The walker (`ge_displaylist`) finds the lists and what they reference;
this module only rewrites them.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Callable, Dict, Iterator, List, Optional, Tuple

from ge_container import MODEL_MATRIX_SEGMENT, BgRoom, ModelImage
from ge_displaylist import (
    G_DL,
    G_DL_NOPUSH,
    G_ENDDL,
    G_MTX,
    G_SETTIMG,
    G_VTX,
    GFX_SIZE,
    VTX_SIZE,
    DisplayListRefs,
    Resolve,
    walk_display_list,
)
from ge_model import NODE_GUNFIRE, NODE_SHADOW, ModelTexture, ModelTree, effect_record
from ge_texture import TexFormat
from ge_vertex import BgVertexKey, ModelVertexKey, VertexBlock
from pack_textures import build_texture_v1_payload

DISPLAY_LIST_RESOURCE_TYPE = "GEDL"
DISPLAY_LIST_RESOURCE_VERSION = 0

# Fast::DisplayList reads this as its first payload byte (`ucode_indy_ge`),
# then skips to the next 8-byte boundary of the file. The payload starts at
# byte 64, so seven pad bytes put the commands at byte 72.
UCODE_INDY_GE = 6
_PAYLOAD_PREFIX_SIZE = 8

G_SETTEX = 0xC0
G_LOADBLOCK = 0xF3
OTR_G_SETTIMG_HASH = 0x20
OTR_G_DL_HASH = 0x31
OTR_G_VTX_HASH = 0x32

# The images the game follows with a call to a list that scrolls them, the
# Caverns water and the water in Dam and Complex (texLoadFromGdl).
WATER_TEXTURES = (1508, 1511)


def water_list_path(texture_id: int) -> str:
    return f"bg/water/{texture_id}"


_EXPANDED_OPCODES = frozenset({OTR_G_SETTIMG_HASH, OTR_G_DL_HASH, OTR_G_VTX_HASH})
_TEXTURE_ID_MASK = 0xFFF
_OFFSET_MASK = 0x00FFFFFF

# One node matrix in segment 3 (an N64 fixed-point Mtx), and the bit that
# marks an address as segmented for the interpreter.
MATRIX_SIZE = 0x40
SEGMENTED_BIT = 1

# N64 (format, pixel size) of a decoded GoldenEye texture, which is what
# G_SETTIMG's fields must say for libultraship to decode the resource's
# bytes. Sizes: 0=4b, 1=8b, 2=16b, 3=32b.
_N64_FORMAT = {
    TexFormat.RGBA32: (0, 3),
    TexFormat.RGBA16: (0, 2),
    TexFormat.RGB24: (0, 3),
    TexFormat.RGB15: (0, 2),
    TexFormat.IA16: (3, 2),
    TexFormat.IA8: (3, 1),
    TexFormat.IA4: (3, 0),
    TexFormat.I8: (4, 1),
    TexFormat.I4: (4, 0),
    TexFormat.RGBA16_CI8: (2, 1),
    TexFormat.RGBA16_CI4: (2, 0),
    TexFormat.IA16_CI8: (2, 1),
    TexFormat.IA16_CI4: (2, 0),
}
# Fast::TextureType by (format, size) for image data taken from a model file.
_TEXTURE_TYPE = {
    (0, 3): 1, (0, 2): 2, (2, 0): 3, (2, 1): 4, (4, 0): 5, (4, 1): 6, (3, 0): 7, (3, 1): 8, (3, 2): 9,
}
_BITS_PER_TEXEL = (4, 8, 16, 32)


class GeDisplayListEmitError(ValueError):
    """Raised when a list cannot be re-emitted faithfully."""


_CRC64_MASK = 0xFFFFFFFFFFFFFFFF


def _crc64_table() -> Tuple[int, ...]:
    table = []
    for byte in range(256):
        crc = byte << 56
        for _ in range(8):
            crc = ((crc << 1) ^ 0x42F0E1EBA9EA3693) & _CRC64_MASK if crc >> 63 else (crc << 1) & _CRC64_MASK
        table.append(crc)
    return tuple(table)


_CRC64_TABLE = _crc64_table()


def path_hash(path: str) -> int:
    """libultraship's `CRC64(const char*)`: CRC-64/ECMA-182 (MSB first),
    all-ones seed, no final inversion. Unlike its `crc64()` sibling, which
    inverts."""
    crc = _CRC64_MASK
    for byte in path.encode("utf-8"):
        crc = _CRC64_TABLE[(crc >> 56) ^ byte] ^ ((crc << 8) & _CRC64_MASK)
    return crc


@dataclass(frozen=True)
class TextureBinding:
    """Where a `G_SETTEX` texture id lives: its texture resource path and
    the N64 format/size/width `G_SETTIMG` must carry to read it. A paletted
    image also names its companion palette resource."""

    path: str
    fmt: int
    size: int
    width: int
    palette_path: Optional[str] = None


def binding_for_format(path: str, fmt: TexFormat, width: int, palette_path: Optional[str] = None) -> TextureBinding:
    n64_fmt, size = _N64_FORMAT[fmt]
    return TextureBinding(path, n64_fmt, size, width, palette_path)


TextureLookup = Callable[[int], TextureBinding]


@dataclass(frozen=True)
class EmbeddedImage:
    """Pixel data a model's `G_SETTIMG` points at inside the model file,
    exactly the texels its `G_LOADBLOCK` loads."""

    path: str
    offset: int
    fmt: int
    size: int
    width: int
    texels: int
    data: bytes


@dataclass(frozen=True)
class Reference:
    kind: str  # "vertex", "list", "texture", "palette", "detail" or "image"
    path: str


@dataclass(frozen=True)
class EmittedList:
    path: str
    payload: bytes
    references: Tuple[Reference, ...]
    source_commands: int  # commands of the original list, ENDDL included


@dataclass
class ReemittedLists:
    lists: Dict[str, EmittedList] = field(default_factory=dict)
    images: Dict[str, EmbeddedImage] = field(default_factory=dict)
    # Path of each root list, keyed by (owner, "primary" | "secondary"):
    # the node offset for a model, the room index for a bg room.
    roots: Dict[Tuple[int, str], str] = field(default_factory=dict)


def encode_embedded_image_payload(image: EmbeddedImage) -> bytes:
    kind = _TEXTURE_TYPE.get((image.fmt, image.size))
    if kind is None:
        # No libultraship type names this pair (an I-format 16-bit image in
        # the legal-page prop); the resource's fmt/size fields, not its
        # type, drive decoding of unflagged textures.
        kind = _TEXTURE_TYPE[(0, image.size)]
    height = -(-image.texels // image.width)
    return build_texture_v1_payload(kind, image.width, height, image.data)


def model_base_path(file_name: str) -> str:
    """`assets/obseg/chr/CarmourguardZ.bin` -> `models/chr/CarmourguardZ`."""
    parts = file_name.split("/")
    return f"models/{parts[-2]}/{parts[-1].rsplit('.', 1)[0]}"


def bg_room_base_path(file_name: str, room: int) -> str:
    """`assets/obseg/bg/bg_sev_all_p.bin`, room 3 -> `bg/bg_sev/room03`."""
    stem = file_name.split("/")[-1].rsplit("_all_p.", 1)[0]
    return f"bg/{stem}/room{room:02d}"


def bg_header_path(file_name: str) -> str:
    """`assets/obseg/bg/bg_sev_all_p.bin` -> `bg/bg_sev/header`."""
    return f"{bg_room_base_path(file_name, 0).rsplit('/', 1)[0]}/header"


def _vertex_command(ref, vertex_path: str, byte_offset: int) -> bytes:
    if ref.count > 0xFF or ref.dest_index + ref.count > 0x7F:
        raise GeDisplayListEmitError(f"G_VTX at {ref.cmd_offset:#x} does not fit the reference encoding")
    w0 = (OTR_G_VTX_HASH << 24) | (ref.count << 12) | ((ref.dest_index + ref.count) << 1)
    return struct.pack(">IIQ", w0, byte_offset, path_hash(vertex_path))


def _reference_command(opcode: int, flags: int, path: str) -> bytes:
    return struct.pack(">IIQ", (opcode << 24) | flags, 0, path_hash(path))


@dataclass
class _Scope:
    """Everything one buffer's lists share while being rewritten."""

    data: bytes
    refs: DisplayListRefs
    list_path: Callable[[int], str]
    image_path: Callable[[int], str]
    vertex_target: Callable[[object], Tuple[str, int]]
    textures: TextureLookup
    result: ReemittedLists


def _scan_list(scope: _Scope, start: int) -> EmittedList:
    vertices = {v.cmd_offset: v for v in scope.refs.vertices}
    timgs = {t.cmd_offset: t for t in scope.refs.textures}
    children = {c.cmd_offset: c for c in scope.refs.child_lists}

    out = bytearray(struct.pack(">B7x", UCODE_INDY_GE))
    references: List[Reference] = []
    pending_image: Optional[Tuple[int, int, int, int]] = None  # offset, fmt, size, width
    commands = 0
    offset = start
    while True:
        w0, w1 = struct.unpack_from(">II", scope.data, offset)
        opcode = w0 >> 24
        commands += 1
        cmd_offset = offset
        offset += GFX_SIZE

        if opcode == G_VTX:
            path, byte_offset = scope.vertex_target(vertices[cmd_offset])
            out += _vertex_command(vertices[cmd_offset], path, byte_offset)
            references.append(Reference("vertex", path))
        elif opcode == G_DL:
            path = scope.list_path(children[cmd_offset].offset)
            flag = (w0 >> 16) & 0xFF
            out += _reference_command(OTR_G_DL_HASH, flag << 16, path)
            references.append(Reference("list", path))
            if flag == G_DL_NOPUSH:
                break  # a jump never returns; the walker stops here too
        elif opcode == G_SETTEX:
            binding = scope.textures(w1 & _TEXTURE_ID_MASK)
            # The original word stays: libultraship ignores it, and the display
            # list factory expands it at load time (src/texture/TexExpand.cpp).
            out += struct.pack(">II", w0, w1)
            out += _reference_command(
                OTR_G_SETTIMG_HASH, (binding.fmt << 21) | (binding.size << 19) | (binding.width - 1), binding.path
            )
            references.append(Reference("texture", binding.path))
            if binding.palette_path is not None:
                references.append(Reference("palette", binding.palette_path))
            if (w1 & _TEXTURE_ID_MASK) in WATER_TEXTURES:
                water_path = water_list_path(w1 & _TEXTURE_ID_MASK)
                out += _reference_command(OTR_G_DL_HASH, 0, water_path)
                references.append(Reference("list", water_path))
            detail_id = (w1 >> 12) & _TEXTURE_ID_MASK
            if detail_id:
                # libultraship has no second texture unit, so no command is
                # emitted for it; the reference keeps the resource reachable.
                # TODO(https://github.com/liz-baker/frigate/issues/317): the
                # detail overlay.
                references.append(Reference("detail", scope.textures(detail_id).path))
        elif opcode == G_SETTIMG:
            if pending_image is not None:
                raise GeDisplayListEmitError(f"G_SETTIMG at {cmd_offset:#x} follows one that was never loaded")
            fmt, size, width = (w0 >> 21) & 7, (w0 >> 19) & 3, (w0 & 0xFFF) + 1
            target = timgs[cmd_offset].offset
            path = scope.image_path(target)
            out += _reference_command(OTR_G_SETTIMG_HASH, w0 & 0x00FFFFFF, path)
            references.append(Reference("image", path))
            pending_image = (target, fmt, size, width)
        elif opcode == G_LOADBLOCK and pending_image is not None:
            _record_image(scope, pending_image, ((w1 >> 12) & 0xFFF) + 1)
            pending_image = None
            out += struct.pack(">II", w0, w1)
        elif opcode == G_MTX and w1 >> 24 == MODEL_MATRIX_SEGMENT:
            if (w1 & _OFFSET_MASK) % MATRIX_SIZE:
                raise GeDisplayListEmitError(f"G_MTX at {cmd_offset:#x}: {w1:#x} is not a whole matrix offset")
            # The interpreter only resolves a segment address whose low bit is
            # set; the walk points segment 3 at the node matrices.
            out += struct.pack(">II", w0, w1 | SEGMENTED_BIT)
        elif opcode == G_ENDDL:
            out += struct.pack(">II", w0, w1)
            break
        else:
            out += struct.pack(">II", w0, w1)

    if pending_image is not None:
        raise GeDisplayListEmitError(f"list at {start:#x}: G_SETTIMG at {pending_image[0]:#x} is never loaded")
    return EmittedList(scope.list_path(start), bytes(out), tuple(references), commands)


def _record_image(scope: _Scope, pending: Tuple[int, int, int, int], texels: int) -> None:
    target, fmt, size, width = pending
    path = scope.image_path(target)
    length = -(-texels * _BITS_PER_TEXEL[size] // 8)
    if target + length > len(scope.data):
        raise GeDisplayListEmitError(f"image at {target:#x} runs off the file")
    known = scope.result.images.get(path)
    if known is not None and (known.fmt, known.size, known.width) != (fmt, size, width):
        raise GeDisplayListEmitError(f"image at {target:#x} is set up with two different formats")
    if known is None or length > len(known.data):
        scope.result.images[path] = EmbeddedImage(
            path, target, fmt, size, width, texels, bytes(scope.data[target : target + length])
        )


def _emit_scope(scope: _Scope, roots: List[Tuple[Tuple[int, str], int]]) -> None:
    lists = sorted(scope.refs.visited_lists)
    for key, start in roots:
        scope.result.roots[key] = scope.list_path(start)
    for start in lists:
        emitted = _scan_list(scope, start)
        scope.result.lists[emitted.path] = emitted


def _walk(data: bytes, roots: List[int], resolve: Resolve, where: str) -> DisplayListRefs:
    combined = DisplayListRefs()
    for root in roots:
        refs = walk_display_list(data, root, resolve)
        if refs.unresolved:
            raise GeDisplayListEmitError(f"{where}: list at {root:#x} has unresolved references")
        combined.vertices.extend(refs.vertices)
        combined.textures.extend(refs.textures)
        combined.child_lists.extend(refs.child_lists)
        combined.visited_lists |= refs.visited_lists
    return combined


def reemit_model_lists(
    model_path: str, image: ModelImage, tree: ModelTree, vertex_blocks: List[VertexBlock], textures: TextureLookup
) -> ReemittedLists:
    """Every list of every display-list node in a model, as
    `<model_path>/dl/<node>/<list>` resources referencing
    `<model_path>/vtx/<node>` and `<model_path>/img/<offset>`."""
    blocks = {b.key.node: b for b in vertex_blocks if isinstance(b.key, ModelVertexKey)}
    result = ReemittedLists()
    for node_offset in sorted(tree.nodes):
        record = tree.nodes[node_offset].display_list
        if record is None:
            continue
        starts = [(kind, s) for kind, s in (("primary", record.primary), ("secondary", record.secondary)) if s is not None]
        if not starts:
            continue
        block = blocks.get(node_offset)
        vertex_path = f"{model_path}/vtx/{node_offset:06x}"

        def vertex_target(ref, block=block, record=record, vertex_path=vertex_path, node_offset=node_offset):
            if block is None:
                raise GeDisplayListEmitError(f"node at {node_offset:#x} loads vertices but has no block")
            relative = ref.offset - record.vertices
            if relative < 0 or relative + ref.count * VTX_SIZE > len(block.vertices) * VTX_SIZE:
                raise GeDisplayListEmitError(f"G_VTX at {ref.cmd_offset:#x} leaves node {node_offset:#x}'s array")
            return vertex_path, relative

        resolve = image.display_list_resolver(record.vertices_address)
        refs = _walk(image.data, [s for _, s in starts], resolve, f"node at {node_offset:#x}")
        scope = _Scope(
            data=image.data,
            refs=refs,
            list_path=lambda offset, node_offset=node_offset: f"{model_path}/dl/{node_offset:06x}/{offset:06x}",
            image_path=lambda offset: f"{model_path}/img/{offset:06x}",
            vertex_target=vertex_target,
            textures=textures,
            result=result,
        )
        _emit_scope(scope, [((node_offset, kind), s) for kind, s in starts])
    return result


# `G_SETTEX` request fields (see "Textures" in
# docs/formats/goldeneye-display-list-resource.md): the type, and the wrap
# mode, wrap-mode and sample offset fields of a tile request.
_REQUEST_TYPE_MIPMAP = 2
_REQUEST_TYPE_TILE_PRESWAPPED = 4
_REQUEST_SAMPLE_OFFSET = 2

_TX_MIRROR = 1
_TX_CLAMP = 2


def _request_wrap_mode(tile_flags: int) -> int:
    """A texture table entry's wrap flags (`G_TX_*`) as a request's mode
    field: 0 wrap, 1 clamp, 2 mirror."""
    if tile_flags & _TX_CLAMP:
        return 1
    return 2 if tile_flags & _TX_MIRROR else 0


def effect_image_request(texture: ModelTexture) -> int:
    """The `G_SETTEX` word a gunfire or shadow node's image is selected
    with: the game's texSelect() loads it as one tile, with a half-texel
    sample offset, through its own wrap flags."""
    kind = _REQUEST_TYPE_MIPMAP if texture.mip_map_tiles else _REQUEST_TYPE_TILE_PRESWAPPED
    return (
        (G_SETTEX << 24)
        | (_request_wrap_mode(texture.s_flags) << 22)
        | (_request_wrap_mode(texture.t_flags) << 20)
        | (_REQUEST_SAMPLE_OFFSET << 18)
        | kind
    )


def effect_image_path(model_path: str, node_offset: int) -> str:
    return f"{model_path}/fx/{node_offset:06x}"


def reemit_effect_images(
    model_path: str, image: ModelImage, tree: ModelTree, textures: TextureLookup
) -> ReemittedLists:
    """One list per textured gunfire or shadow node, at
    `<model_path>/fx/<node>`: the texture request and its reference, the same
    two commands a model list holding it would carry."""
    result = ReemittedLists()
    for node_offset in sorted(tree.nodes):
        node = tree.nodes[node_offset]
        if node.node_type not in (NODE_GUNFIRE, NODE_SHADOW):
            continue
        record = effect_record(image, tree, node)
        if record is None or record.texture is None:
            continue
        binding = textures(record.texture.texture_id)
        out = bytearray(struct.pack(">B7x", UCODE_INDY_GE))
        out += struct.pack(">II", effect_image_request(record.texture), record.texture.texture_id)
        out += _reference_command(
            OTR_G_SETTIMG_HASH, (binding.fmt << 21) | (binding.size << 19) | (binding.width - 1), binding.path
        )
        out += struct.pack(">II", G_ENDDL << 24, 0)
        references = [Reference("texture", binding.path)]
        if binding.palette_path is not None:
            references.append(Reference("palette", binding.palette_path))
        path = effect_image_path(model_path, node_offset)
        result.lists[path] = EmittedList(path, bytes(out), tuple(references), 3)
    return result


def reemit_bg_room_lists(
    room_path: str, room: BgRoom, vertex_blocks: List[VertexBlock], textures: TextureLookup
) -> ReemittedLists:
    """A bg room's primary and secondary lists, as
    `<room_path>/dl/<primary|secondary>/<list>` resources referencing
    `<room_path>/vtx/<blob offset>`."""
    blocks = sorted(b.key.offset for b in vertex_blocks if isinstance(b.key, BgVertexKey) and b.key.room == room.index)
    lengths = {b.key.offset: len(b.vertices) * VTX_SIZE for b in vertex_blocks if isinstance(b.key, BgVertexKey)}
    result = ReemittedLists()

    def vertex_target(ref):
        for begin in blocks:
            if begin <= ref.offset and ref.offset + ref.count * VTX_SIZE <= begin + lengths[begin]:
                return f"{room_path}/vtx/{begin:06x}", ref.offset - begin
        raise GeDisplayListEmitError(f"room {room.index}: G_VTX at {ref.cmd_offset:#x} is in no vertex block")

    def no_images(offset):
        raise GeDisplayListEmitError(f"room {room.index}: G_SETTIMG at {offset:#x}, but bg lists load textures by id")

    for kind, blob in (("primary", room.primary), ("secondary", room.secondary)):
        if blob is None:
            continue
        scope = _Scope(
            data=blob,
            refs=_walk(blob, [0], room.resolve, f"room {room.index} {kind}"),
            list_path=lambda offset, kind=kind: f"{room_path}/dl/{kind}/{offset:06x}",
            image_path=no_images,
            vertex_target=vertex_target,
            textures=textures,
            result=result,
        )
        _emit_scope(scope, [((room.index, kind), 0)])
    return result


def reemit_segment_lists(
    base_path: str,
    data: bytes,
    roots: List[int],
    resolve: Resolve,
    vertex_ranges: List[Tuple[int, int]],
    textures: TextureLookup,
) -> ReemittedLists:
    """The lists reachable from `roots` in a plain data segment (lists,
    vertices and images laid out in one buffer with no container), as
    `<base_path>/dl/<offset>` resources referencing `<base_path>/vtx/<offset>`
    and `<base_path>/img/<offset>`. `vertex_ranges` are the (offset, byte
    length) spans the caller writes as vertex resources, named by their
    start offset; every G_VTX must fall inside one."""
    result = ReemittedLists()

    def vertex_target(ref):
        for begin, length in vertex_ranges:
            if begin <= ref.offset and ref.offset + ref.count * VTX_SIZE <= begin + length:
                return f"{base_path}/vtx/{begin:06x}", ref.offset - begin
        raise GeDisplayListEmitError(f"G_VTX at {ref.cmd_offset:#x} is in no vertex range")

    scope = _Scope(
        data=data,
        refs=_walk(data, roots, resolve, base_path),
        list_path=lambda offset: f"{base_path}/dl/{offset:06x}",
        image_path=lambda offset: f"{base_path}/img/{offset:06x}",
        vertex_target=vertex_target,
        textures=textures,
        result=result,
    )
    _emit_scope(scope, [((offset, "root"), offset) for offset in roots])
    return result


def iter_commands(payload: bytes) -> Iterator[Tuple[int, int, int, Optional[int]]]:
    """Decodes an emitted payload's commands, treating the 16-byte
    reference forms as one command: yields (opcode, w0, w1, path hash or
    None). Used to check what was emitted."""
    at = _PAYLOAD_PREFIX_SIZE
    while at < len(payload):
        w0, w1 = struct.unpack_from(">II", payload, at)
        at += GFX_SIZE
        opcode = w0 >> 24
        digest = None
        if opcode in _EXPANDED_OPCODES:
            hi, lo = struct.unpack_from(">II", payload, at)
            at += GFX_SIZE
            digest = (hi << 32) | lo
        yield opcode, w0, w1, digest
