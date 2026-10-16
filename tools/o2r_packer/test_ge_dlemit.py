import struct
import unittest

from ge_container import BgRoom, ModelImage
from ge_displaylist import G_DL, G_DL_NOPUSH, G_ENDDL, G_SETTIMG, G_VTX, VTX_SIZE
from ge_dlemit import (
    G_LOADBLOCK,
    G_SETTEX,
    OTR_G_DL_HASH,
    OTR_G_SETTIMG_HASH,
    OTR_G_VTX_HASH,
    REFERENCE_SETS_TEXTURE_LEVEL,
    UCODE_INDY_GE,
    GeDisplayListEmitError,
    TextureBinding,
    bg_header_path,
    bg_room_base_path,
    encode_embedded_image_payload,
    iter_commands,
    model_base_path,
    path_hash,
    effect_image_path,
    effect_image_request,
    reemit_bg_room_lists,
    reemit_effect_images,
    reemit_model_lists,
)
from ge_model import NODE_DL, ModelCounts, ModelTexture, parse_model_tree
from ge_vertex import extract_bg_vertices, extract_model_vertices

SEG5 = 0x05000000
SEG4 = 0x04000000
SEG_BG = 0x0E000000
NODE_SIZE = 0x18
RECORD_SIZE = 0x14
NOP = 0xE7000000  # an RDP command with no addresses


def cmd(w0, w1=0):
    return struct.pack(">II", w0, w1)


def vtx(count, dest, addr):
    return cmd((G_VTX << 24) | (dest << 16) | (count * VTX_SIZE), addr)


def call(addr, flag=0):
    return cmd((G_DL << 24) | (flag << 16), addr)


def settex(texture_id, upper=0, request_type=2):
    return cmd(G_SETTEX << 24 | 0x080000 | request_type, (upper << 12) | texture_id)


END = cmd(G_ENDDL << 24)
BINDING = TextureBinding("textures/WALL", fmt=0, size=2, width=32, detail=0x38D2)
PALETTED = TextureBinding("textures/CRATE", fmt=2, size=1, width=16, palette_path="textures/CRATE.pal")
WATER = TextureBinding("textures/WATER", fmt=2, size=1, width=32, palette_path="textures/WATER.pal")
BINDINGS = {5: BINDING, 9: PALETTED, 1511: WATER}


def lookup(texture_id):
    return BINDINGS[texture_id]


def commands(payload):
    return list(iter_commands(payload))


class PathHashTest(unittest.TestCase):
    def test_matches_libultraship_crc64(self):
        # Values from compiling the library's own `CRC64(const char*)`.
        self.assertEqual(path_hash("a"), 0x318C0BD8533F5665)
        self.assertEqual(path_hash("textures/X"), 0x95529FC3737D382C)
        self.assertEqual(path_hash("models/chr/CarmourguardZ/vtx/000048"), 0xFFA559057E7BDFFF)


class PathHelpersTest(unittest.TestCase):
    def test_model_path(self):
        self.assertEqual(model_base_path("assets/obseg/chr/CarmourguardZ.bin"), "models/chr/CarmourguardZ")

    def test_bg_room_path(self):
        self.assertEqual(bg_room_base_path("assets/obseg/bg/bg_sev_all_p.bin", 3), "bg/bg_sev/room03")
        self.assertEqual(bg_header_path("assets/obseg/bg/bg_sev_all_p.bin"), "bg/bg_sev/header")


class ModelEmitTest(unittest.TestCase):
    def build(self, lists, vertex_count=4, embedded=b""):
        """One DL node at 0; its record, then the vertex array, the
        embedded image bytes and the lists. `lists` gets the lists' own
        offset so a list can name its siblings."""
        record_at = NODE_SIZE
        vertices_at = record_at + RECORD_SIZE
        embedded_at = vertices_at + vertex_count * VTX_SIZE
        lists_at = embedded_at + len(embedded)
        body = lists(lists_at, embedded_at)
        record = struct.pack(">IIIIH", SEG5 + lists_at, 0, 0, SEG5 + vertices_at, vertex_count).ljust(RECORD_SIZE, b"\0")
        root = struct.pack(">HHIIIII", NODE_DL, 0, SEG5 + record_at, 0, 0, 0, 0)
        vertices = b"".join(struct.pack(">3hH2h4B", i, i, i, 0, 0, 0, 1, 2, 3, 4) for i in range(vertex_count))
        image = ModelImage(root + record + vertices + embedded + body)
        tree = parse_model_tree(image, ModelCounts(0, 0))
        return image, tree, extract_model_vertices(image, tree), lists_at

    def emit(self, lists, **kwargs):
        image, tree, blocks, lists_at = self.build(lists, **kwargs)
        return reemit_model_lists("models/prop/Ptest", image, tree, blocks, lookup), lists_at

    def test_vertex_load_becomes_a_path_reference_with_a_block_relative_offset(self):
        result, lists_at = self.emit(lambda at, _: vtx(2, 3, SEG4 + 2 * VTX_SIZE) + END)
        emitted = result.lists[f"models/prop/Ptest/dl/000000/{lists_at:06x}"]
        first = commands(emitted.payload)[0]
        opcode, w0, offset, digest = first
        self.assertEqual(opcode, OTR_G_VTX_HASH)
        self.assertEqual(offset, 2 * VTX_SIZE)
        # count in bits 12-19; buffer end slot (dest + count) in bits 1-7
        self.assertEqual(((w0 >> 12) & 0xFF, (w0 >> 1) & 0x7F), (2, 5))
        self.assertEqual(digest, path_hash("models/prop/Ptest/vtx/000000"))
        self.assertEqual([r.kind for r in emitted.references], ["vertex"])

    def test_payload_layout(self):
        result, lists_at = self.emit(lambda at, _: cmd(NOP) + END)
        payload = result.lists[f"models/prop/Ptest/dl/000000/{lists_at:06x}"].payload
        self.assertEqual([c[0] for c in commands(payload)], [NOP >> 24, G_ENDDL])
        self.assertEqual(payload[:8], bytes([UCODE_INDY_GE]) + bytes(7))
        self.assertEqual(payload[8:], cmd(NOP) + END)

    def test_settex_keeps_its_word_and_adds_a_texture_reference(self):
        result, lists_at = self.emit(lambda at, _: settex(5) + END)
        emitted = result.lists[f"models/prop/Ptest/dl/000000/{lists_at:06x}"]
        original, settimg, *_ = commands(emitted.payload)
        self.assertEqual((original[0], original[1], original[2]), (G_SETTEX, G_SETTEX << 24 | 0x080002, 5))
        opcode, w0, _, digest = settimg
        self.assertEqual(opcode, OTR_G_SETTIMG_HASH)
        self.assertEqual((w0 >> 21 & 7, w0 >> 19 & 3, w0 & 0xFFF), (0, 2, 31))
        self.assertEqual(digest, path_hash("textures/WALL"))
        self.assertEqual([(r.kind, r.path) for r in emitted.references], [("texture", "textures/WALL")])

    def test_water_texture_is_followed_by_a_call_to_its_scrolling_list(self):
        result, lists_at = self.emit(lambda at, _: settex(1511) + END)
        emitted = result.lists[f"models/prop/Ptest/dl/000000/{lists_at:06x}"]
        settex_command, settimg, water_call, end = commands(emitted.payload)
        self.assertEqual(settimg[0], OTR_G_SETTIMG_HASH)
        self.assertEqual((water_call[0], water_call[3]), (OTR_G_DL_HASH, path_hash("bg/water/1511")))
        self.assertEqual(end[0], G_ENDDL)
        self.assertIn(("list", "bg/water/1511"), [(r.kind, r.path) for r in emitted.references])

    def test_detail_request_references_its_detail_image_second(self):
        result, lists_at = self.emit(lambda at, _: settex(9, upper=5, request_type=1) + END)
        emitted = result.lists[f"models/prop/Ptest/dl/000000/{lists_at:06x}"]
        self.assertEqual(
            [(r.kind, r.path) for r in emitted.references],
            [("texture", "textures/CRATE"), ("palette", "textures/CRATE.pal"), ("detail", "textures/WALL")],
        )
        _, image, detail, _ = commands(emitted.payload)
        self.assertEqual(
            (image[0], image[2], image[3]),
            (OTR_G_SETTIMG_HASH, REFERENCE_SETS_TEXTURE_LEVEL, path_hash("textures/CRATE")),
        )
        self.assertEqual(detail[0], OTR_G_SETTIMG_HASH)
        self.assertEqual((detail[1] >> 21 & 7, detail[1] >> 19 & 3, detail[1] & 0xFFF), (0, 2, 31))
        # The detail image's own detail-tile fields ride in the reference.
        self.assertEqual((detail[2], detail[3]), (0x38D2, path_hash("textures/WALL")))

    def test_other_requests_have_no_detail_image(self):
        # Bits 12 and up mean something else outside a detail request.
        result, lists_at = self.emit(lambda at, _: settex(9, upper=5, request_type=0) + END)
        emitted = result.lists[f"models/prop/Ptest/dl/000000/{lists_at:06x}"]
        self.assertEqual(
            [r.kind for r in emitted.references],
            ["texture", "palette"],
        )
        self.assertEqual(sum(1 for c in commands(emitted.payload) if c[0] == OTR_G_SETTIMG_HASH), 1)

    def test_texture_reference_carries_the_image_detail_fields(self):
        result, lists_at = self.emit(lambda at, _: settex(5, request_type=0) + END)
        emitted = result.lists[f"models/prop/Ptest/dl/000000/{lists_at:06x}"]
        settimg = commands(emitted.payload)[1]
        # The model list's request gets its G_TEXTURE level set, as on load in the game.
        self.assertEqual((settimg[0], settimg[2]), (OTR_G_SETTIMG_HASH, REFERENCE_SETS_TEXTURE_LEVEL | 0x38D2))

    def test_segment_3_matrix_address_is_marked_segmented(self):
        result, lists_at = self.emit(lambda at, _: cmd(0x01030000, 0x03000080) + END)
        payload = result.lists[f"models/prop/Ptest/dl/000000/{lists_at:06x}"].payload
        self.assertEqual(payload[8:16], cmd(0x01030000, 0x03000081))

    def test_matrix_address_between_matrices_is_an_error(self):
        with self.assertRaises(GeDisplayListEmitError):
            self.emit(lambda at, _: cmd(0x01030000, 0x03000020) + END)

    def test_call_becomes_a_reference_to_the_child_list_resource(self):
        def lists(at, _):
            return call(SEG5 + at + 3 * 8) + cmd(NOP) + END + cmd(NOP) + END

        result, lists_at = self.emit(lists)
        parent = result.lists[f"models/prop/Ptest/dl/000000/{lists_at:06x}"]
        child_path = f"models/prop/Ptest/dl/000000/{lists_at + 24:06x}"
        self.assertIn(child_path, result.lists)
        opcode, w0, _, digest = commands(parent.payload)[0]
        self.assertEqual((opcode, (w0 >> 16) & 1), (OTR_G_DL_HASH, 0))
        self.assertEqual(digest, path_hash(child_path))
        self.assertEqual(parent.source_commands, 3)

    def test_jump_keeps_its_flag_and_ends_the_list(self):
        def lists(at, _):
            return call(SEG5 + at + 8, G_DL_NOPUSH) + END

        result, lists_at = self.emit(lists)
        parent = result.lists[f"models/prop/Ptest/dl/000000/{lists_at:06x}"]
        (jump,) = commands(parent.payload)
        self.assertEqual((jump[0], (jump[1] >> 16) & 0xFF), (OTR_G_DL_HASH, G_DL_NOPUSH))
        self.assertEqual(parent.source_commands, 1)

    def test_embedded_image_is_extracted_to_the_loaded_span(self):
        pixels = bytes(range(64))

        def lists(at, image_at):
            return (
                cmd(G_SETTIMG << 24 | 2 << 19 | 7, SEG5 + image_at)
                + cmd(NOP)
                + cmd(G_LOADBLOCK << 24, 0x07000000 | (15 << 12))  # 16 texels of 16 bits
                + END
            )

        result, lists_at = self.emit(lists, embedded=pixels)
        (image,) = result.images.values()
        self.assertEqual(image.path, f"models/prop/Ptest/img/{image.offset:06x}")
        self.assertEqual((image.fmt, image.size, image.width, image.texels), (0, 2, 8, 16))
        self.assertEqual(image.data, pixels[:32])
        emitted = result.lists[f"models/prop/Ptest/dl/000000/{lists_at:06x}"]
        opcode, w0, _, digest = commands(emitted.payload)[0]
        self.assertEqual((opcode, w0 & 0xFFFFFF), (OTR_G_SETTIMG_HASH, 2 << 19 | 7))
        self.assertEqual(digest, path_hash(image.path))
        self.assertEqual(emitted.references[0].kind, "image")

        payload = encode_embedded_image_payload(image)
        type_, width, height, flags = struct.unpack_from(">IIII", payload)
        self.assertEqual((type_, width, height, flags), (2, 8, 2, 0))
        self.assertEqual(payload[-32:], pixels[:32])

    def test_image_that_is_never_loaded_is_an_error(self):
        with self.assertRaises(GeDisplayListEmitError):
            self.emit(lambda at, image_at: cmd(G_SETTIMG << 24 | 2 << 19 | 7, SEG5 + image_at) + END, embedded=bytes(32))

    def test_vertex_load_outside_the_nodes_array_is_an_error(self):
        with self.assertRaises(GeDisplayListEmitError):
            self.emit(lambda at, _: vtx(2, 0, SEG4 + 3 * VTX_SIZE) + END)


class EffectImageTest(unittest.TestCase):
    def texture(self, mip_map_tiles=0, s_flags=0, t_flags=0):
        return ModelTexture(5, 32, 16, mip_map_tiles, 0, 3, s_flags, t_flags)

    def test_a_single_level_image_is_a_preswapped_tile_request_with_a_half_texel_offset(self):
        self.assertEqual(effect_image_request(self.texture()), 0xC0080004)

    def test_a_mipmapped_image_is_a_mipmap_request(self):
        self.assertEqual(effect_image_request(self.texture(mip_map_tiles=6)) & 7, 2)

    def test_wrap_flags_become_request_wrap_modes(self):
        # clamp -> 1, mirror -> 2, wrap -> 0; s in bits 22-23, t in bits 20-21.
        word = effect_image_request(self.texture(s_flags=2, t_flags=1))
        self.assertEqual(((word >> 22) & 3, (word >> 20) & 3), (1, 2))

    def build(self, node_type):
        # A texture table of one entry (id 5, 32x16, no mip levels), then one
        # effect node at 0x0c whose data record is empty but for its image.
        table = struct.pack(">I7Bx", 5, 32, 16, 0, 0, 3, 0, 0)
        data = 0x0C + NODE_SIZE
        record = struct.pack(">3f3fI", 0, 0, 0, 0, 0, 0, SEG5) if node_type == 0x0C else struct.pack(">2f2fII", 0, 0, 0, 0, SEG5, 0)
        blob = table + struct.pack(">HHIIIII", node_type, 0, SEG5 + data, 0, 0, 0, 0) + record + b"\0" * 16
        image = ModelImage(blob)
        return image, parse_model_tree(image, ModelCounts(0, 1))

    def test_a_textured_gunfire_node_gets_a_request_and_a_texture_reference(self):
        image, tree = self.build(0x0C)
        result = reemit_effect_images("models/gun/Gfoo", image, tree, lookup)
        path = effect_image_path("models/gun/Gfoo", 0x0C)
        self.assertEqual(list(result.lists), [path])
        emitted = result.lists[path]
        self.assertEqual(emitted.payload[0], UCODE_INDY_GE)
        request, texture_id = struct.unpack_from(">II", emitted.payload, 8)
        self.assertEqual((request, texture_id), (effect_image_request(self.texture()), 5))
        w0, w1, digest = struct.unpack_from(">IIQ", emitted.payload, 16)
        self.assertEqual(w0, (OTR_G_SETTIMG_HASH << 24) | (0 << 21) | (2 << 19) | 31)
        # texSelect builds this list: the caller's G_TEXTURE stays as it is.
        self.assertEqual(w1, 0)
        self.assertEqual(digest, path_hash("textures/WALL"))
        self.assertEqual(emitted.payload[32:], cmd(G_ENDDL << 24))

    def test_a_shadow_node_gets_one_too_and_other_nodes_none(self):
        image, tree = self.build(0x0D)
        self.assertEqual(len(reemit_effect_images("models/chr/Cfoo", image, tree, lookup).lists), 1)
        image, tree = self.build(0x12)
        self.assertEqual(reemit_effect_images("models/chr/Cfoo", image, tree, lookup).lists, {})


class BgEmitTest(unittest.TestCase):
    def room(self, primary, secondary=None, vertex_count=8):
        return BgRoom(3, bytes(vertex_count * VTX_SIZE), primary, secondary)

    def emit(self, room):
        return reemit_bg_room_lists("bg/bg_test/room03", room, extract_bg_vertices(room), lookup)

    def test_primary_and_secondary_lists_and_a_vertex_offset_into_a_merged_block(self):
        # Two loads that touch merge into one block at 0x10; the second
        # points into its middle.
        primary = vtx(2, 0, SEG_BG + 0x10) + vtx(2, 2, SEG_BG + 0x30) + settex(5) + END
        secondary = vtx(1, 0, SEG_BG + 0x40) + END
        result = self.emit(self.room(primary, secondary))
        self.assertEqual(sorted(result.roots), [(3, "primary"), (3, "secondary")])
        first = result.lists[result.roots[(3, "primary")]]
        _, _, offset_a, digest_a = commands(first.payload)[0]
        _, _, offset_b, digest_b = commands(first.payload)[1]
        self.assertEqual(result.roots[(3, "primary")], "bg/bg_test/room03/dl/primary/000000")
        self.assertEqual((offset_a, offset_b), (0, 0x20))
        self.assertEqual(len(extract_bg_vertices(self.room(primary, secondary))), 1)
        self.assertEqual(digest_a, path_hash("bg/bg_test/room03/vtx/000010"))
        self.assertEqual(digest_b, digest_a)
        # the secondary load touches the first block's end, so it is the same block
        _, _, offset_c, digest_c = commands(result.lists[result.roots[(3, "secondary")]].payload)[0]
        self.assertEqual((offset_c, digest_c), (0x30, digest_a))

    def test_a_call_bg_lists_cannot_resolve_is_an_error(self):
        # Room lists reach only the vertex blob's segment; a G_DL would
        # be unresolved for the walker too.
        with self.assertRaises(GeDisplayListEmitError):
            reemit_bg_room_lists("bg/bg_test/room03", self.room(call(SEG_BG + 0x10) + END), [], lookup)

    def test_image_load_in_a_bg_list_is_an_error(self):
        with self.assertRaises(GeDisplayListEmitError):
            self.emit(self.room(cmd(G_SETTIMG << 24, SEG_BG) + END))


if __name__ == "__main__":
    unittest.main()
