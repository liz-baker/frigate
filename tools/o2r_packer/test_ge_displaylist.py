import struct
import unittest

from ge_displaylist import (
    G_DL, G_ENDDL, G_MTX, G_SETTIMG, G_VTX, VTX_SIZE, merge_vertex_ranges, walk_display_list,
)

SEG = 0x0E000000


def cmd(w0, w1=0):
    return struct.pack(">II", w0, w1)


def vtx(count, dest, addr):
    return cmd((G_VTX << 24) | (dest << 16) | (count * VTX_SIZE), addr)


def call(addr, flag=0):
    return cmd((G_DL << 24) | (flag << 16), addr)


def settimg(fmt, size, addr):
    return cmd((G_SETTIMG << 24) | (fmt << 21) | (size << 19), addr)


END = cmd(G_ENDDL << 24)


def resolve(addr):
    return addr - SEG if addr >> 24 == 0x0E else None


class WalkerTest(unittest.TestCase):
    def test_collects_references_until_enddl(self):
        data = vtx(8, 2, SEG + 0x100) + settimg(2, 2, SEG + 0x200) + cmd(G_MTX << 24, SEG + 0x300) + END
        data += cmd(0xB1 << 24)  # after ENDDL: must not be read
        refs = walk_display_list(data, 0, resolve)

        self.assertEqual([(v.offset, v.count, v.dest_index) for v in refs.vertices], [(0x100, 8, 2)])
        self.assertEqual([(t.offset, t.fmt, t.size) for t in refs.textures], [(0x200, 2, 2)])
        self.assertEqual([m.offset for m in refs.matrices], [0x300])
        self.assertEqual(refs.command_count, 4)
        self.assertEqual(refs.unresolved, [])

    def test_call_follows_child_then_returns(self):
        data = call(SEG + 0x20) + vtx(1, 0, SEG + 0x100) + END
        data += b"\0" * (0x20 - len(data))
        data += vtx(4, 0, SEG + 0x140) + END
        refs = walk_display_list(data, 0, resolve)

        self.assertEqual(sorted(v.offset for v in refs.vertices), [0x100, 0x140])
        self.assertEqual([c.offset for c in refs.child_lists], [0x20])

    def test_branch_never_returns(self):
        data = call(SEG + 0x20, flag=1) + vtx(1, 0, SEG + 0x100) + END
        data += b"\0" * (0x20 - len(data)) + END
        refs = walk_display_list(data, 0, resolve)

        self.assertEqual(refs.vertices, [])

    def test_shared_child_visited_once(self):
        data = call(SEG + 0x18) + call(SEG + 0x18) + END + vtx(2, 0, SEG + 0x100) + END
        refs = walk_display_list(data, 0, resolve)

        self.assertEqual(len(refs.vertices), 1)

    def test_recursive_call_terminates(self):
        refs = walk_display_list(call(SEG) + END, 0, resolve)

        self.assertEqual(refs.visited_lists, {0})

    def test_unresolvable_address_is_reported(self):
        refs = walk_display_list(vtx(1, 0, 0x05000000) + END, 0, resolve)

        self.assertEqual(refs.unresolved, [0])
        self.assertEqual(refs.vertices, [])

    def test_running_off_the_buffer_is_reported(self):
        refs = walk_display_list(cmd(0xBF << 24), 0, resolve)

        self.assertEqual(refs.unresolved, [8])

    def test_command_cap(self):
        data = cmd(0xBF << 24) * 100
        refs = walk_display_list(data, 0, resolve, max_commands=10)

        self.assertEqual(refs.command_count, 10)
        self.assertEqual(refs.unresolved, [80])

    def test_merge_vertex_ranges(self):
        data = vtx(4, 0, SEG + 0x100) + vtx(4, 0, SEG + 0x120) + vtx(2, 0, SEG + 0x200) + END
        refs = walk_display_list(data, 0, resolve)

        self.assertEqual(merge_vertex_ranges(refs), [(0x100, 0x60), (0x200, 0x20)])


if __name__ == "__main__":
    unittest.main()
