import struct
import tempfile
import unittest
from pathlib import Path

from ge_model import GeModelError
from ge_skeleton import JOINT_SIZE, SkeletonJoint, encode_skeleton_payload, parse_skeletons


class ParseSkeletonsTest(unittest.TestCase):
    def _decomp(self, files):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        for relative, text in files.items():
            path = root / "assets" / "embedded" / "skeletons" / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        return root

    def test_reads_joint_list_by_skeleton_name(self):
        root = self._decomp(
            {
                "guard.inc.c": (
                    "ModelJoint JOINTLIST(guard)[] = {\n"
                    "    {0x401, 0x0, 0x0},\n"
                    "    {0x02, 0x0F, 0x12},\n"
                    "};\n"
                    "MODELSKELETON(guard, 2, 0x2D)\n"
                )
            }
        )
        skeletons = parse_skeletons(root)
        self.assertEqual(
            skeletons["guard"],
            [SkeletonJoint(0x401, 0x0, 0x0), SkeletonJoint(0x02, 0x0F, 0x12)],
        )

    def test_reads_an_array_with_an_explicit_size(self):
        root = self._decomp(
            {"cctv.inc.c": "ModelJoint JOINTLIST(cctv)[2] = {\n    {0x401, 0, 0},\n    {2, 1, 1},\n};\n"}
        )
        skeletons = parse_skeletons(root)
        self.assertEqual(len(skeletons["cctv"]), 2)

    def test_one_file_can_hold_several_skeletons(self):
        root = self._decomp(
            {
                "props.inc.c": (
                    "ModelJoint JOINTLIST(cctv)[] = {\n    {0x401, 0, 0},\n};\n"
                    "MODELSKELETON(cctv, 1, 4)\n"
                    "ModelJoint JOINTLIST(door)[] = {\n    {0x401, 0, 0},\n    {2, 1, 1},\n};\n"
                    "MODELSKELETON(door, 2, 4)\n"
                )
            }
        )
        skeletons = parse_skeletons(root)
        self.assertEqual(set(skeletons), {"cctv", "door"})
        self.assertEqual(len(skeletons["door"]), 2)

    def test_rejects_a_joint_count_mismatch(self):
        root = self._decomp(
            {
                "bad.inc.c": (
                    "ModelJoint JOINTLIST(bad)[] = {\n    {0x401, 0, 0},\n};\n"
                    "MODELSKELETON(bad, 2, 4)\n"
                )
            }
        )
        with self.assertRaisesRegex(GeModelError, "MODELSKELETON names 2 joints but JOINTLIST has 1"):
            parse_skeletons(root)


class EncodeSkeletonPayloadTest(unittest.TestCase):
    def test_encodes_count_and_joint_records(self):
        joints = [SkeletonJoint(0x401, 0x00, 0x00), SkeletonJoint(0x02, 0x0F, 0x12)]
        payload = encode_skeleton_payload(joints)
        self.assertEqual(len(payload), 4 + 2 * JOINT_SIZE)
        count = struct.unpack_from(">I", payload)[0]
        self.assertEqual(count, 2)
        first = struct.unpack_from(">HHH", payload, 4)
        second = struct.unpack_from(">HHH", payload, 4 + JOINT_SIZE)
        self.assertEqual(first, (0x401, 0x00, 0x00))
        self.assertEqual(second, (0x02, 0x0F, 0x12))


if __name__ == "__main__":
    unittest.main()
