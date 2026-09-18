import struct
import unittest
import zlib

from ge_container import GeContainerError, unwrap_bg_file, unwrap_model_file
from ge_displaylist import G_ENDDL, G_MTX, G_VTX, VTX_SIZE, walk_display_list


def rzip(payload: bytes) -> bytes:
    """Wraps `payload` the way the game's files do: a two-byte marker
    then raw DEFLATE."""
    compressor = zlib.compressobj(wbits=-15)
    return b"\x11\x72" + compressor.compress(payload) + compressor.flush()


def cmd(w0, w1=0):
    return struct.pack(">II", w0, w1)


def vtx(count, addr):
    return cmd((G_VTX << 24) | (count * VTX_SIZE), addr)


END = cmd(G_ENDDL << 24)


def build_bg(rooms):
    """rooms: list of (vertices|None, primary, secondary|None). Appends
    the end-of-table sentinel entry the real files carry."""
    table_at = 0x14
    entry_count = len(rooms) + 2  # placeholder 0, rooms, sentinel
    body_at = table_at + 24 * (entry_count + 1)  # +1: null terminator
    body = b""
    entries = [(0, 0, 0)]

    def place(blob):
        nonlocal body
        if blob is None:
            return 0
        pointer = 0x0F000000 | (body_at + len(body))
        body += rzip(blob)
        return pointer

    for vertices, primary, secondary in rooms:
        entries.append((place(vertices), place(primary), place(secondary)))
    end = 0x0F000000 | (body_at + len(body))
    entries.append((end, end, end))
    entries.append((0, 0, 0))

    header = struct.pack(">II", 0, 0x0F000000 | table_at).ljust(table_at, b"\0")
    table = b"".join(struct.pack(">IIIfff", *e, 0.0, 0.0, 0.0) for e in entries)
    return header + table + body


class ModelUnwrapTest(unittest.TestCase):
    def test_inflates_and_resolves_segment_five(self):
        payload = bytes(range(64))
        image = unwrap_model_file(rzip(payload))
        self.assertEqual(image.data, payload)
        self.assertEqual(image.resolve(0x05000010), 0x10)

    def test_resolve_rejects_other_segments_and_out_of_range(self):
        image = unwrap_model_file(rzip(bytes(64)))
        self.assertIsNone(image.resolve(0))
        self.assertIsNone(image.resolve(0x06000010))
        self.assertIsNone(image.resolve(0x05000040))

    def test_bad_header_is_a_container_error(self):
        with self.assertRaises(GeContainerError):
            unwrap_model_file(b"\x00\x00nonsense")

    def test_corrupt_stream_is_a_container_error(self):
        with self.assertRaises(GeContainerError):
            unwrap_model_file(b"\x11\x72\xff\xff\xff\xff")

    def test_node_resolver_binds_vertex_and_matrix_segments(self):
        vertices_at = 0x40
        dl = cmd(G_MTX << 24, 0x03000140) + vtx(2, 0x04000010) + END
        payload = dl.ljust(vertices_at, b"\0") + bytes(64)
        image = unwrap_model_file(rzip(payload))

        resolve = image.display_list_resolver(0x05000000 | vertices_at)
        refs = walk_display_list(image.data, 0, resolve)

        self.assertEqual(refs.unresolved, [])
        self.assertEqual([v.offset for v in refs.vertices], [vertices_at + 0x10])
        self.assertEqual([m.offset for m in refs.matrices], [0x140])

    def test_node_resolver_without_vertex_array_leaves_vtx_unresolved(self):
        payload = vtx(1, 0x04000000) + END
        image = unwrap_model_file(rzip(payload))
        refs = walk_display_list(image.data, 0, image.display_list_resolver(0))
        self.assertEqual(refs.unresolved, [0])


class BgUnwrapTest(unittest.TestCase):
    def test_rooms_decompress_in_order_without_the_sentinel(self):
        rooms = [
            (bytes(32), vtx(2, 0x0E000000) + END, END),
            (bytes(48), vtx(3, 0x0E000010) + END, None),
        ]
        unwrapped = unwrap_bg_file(build_bg(rooms))

        self.assertEqual([r.index for r in unwrapped], [1, 2])
        self.assertEqual(unwrapped[0].vertices, rooms[0][0])
        self.assertEqual(unwrapped[0].secondary, END)
        self.assertEqual(unwrapped[1].primary, rooms[1][1])
        self.assertIsNone(unwrapped[1].secondary)

    def test_room_without_vertices_has_an_empty_blob(self):
        unwrapped = unwrap_bg_file(build_bg([(None, END, None), (bytes(16), END, None)]))
        self.assertEqual(unwrapped[0].vertices, b"")

    def test_room_resolver_maps_segment_0e_into_vertex_blob(self):
        room = unwrap_bg_file(build_bg([(bytes(32), vtx(2, 0x0E000010) + END, None)]))[0]
        refs = walk_display_list(room.primary, 0, room.resolve)
        self.assertEqual(refs.unresolved, [])
        self.assertEqual([v.offset for v in refs.vertices], [0x10])
        self.assertIsNone(room.resolve(0x0E000020))
        self.assertIsNone(room.resolve(0x05000000))

    def test_pointer_outside_the_file_is_rejected(self):
        data = bytearray(build_bg([(bytes(16), END, None), (bytes(16), END, None)]))
        struct.pack_into(">I", data, 0x14 + 24 + 4, 0x0F00FFFF)
        with self.assertRaises(GeContainerError):
            unwrap_bg_file(bytes(data))

    def test_truncated_file_is_rejected(self):
        with self.assertRaises(GeContainerError):
            unwrap_bg_file(b"\0\0\0\0")
        with self.assertRaises(GeContainerError):
            unwrap_bg_file(build_bg([(bytes(16), END, None)])[:0x30])


if __name__ == "__main__":
    unittest.main()
