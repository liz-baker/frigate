import tempfile
import unittest
import zipfile
from pathlib import Path

from build_f3d_o2r import build_f3d_o2r
from o2r_writer import OTR_HEADER_SIZE, unpack_resource_header


class BuildF3dO2rTest(unittest.TestCase):
    def test_writes_each_shader_at_its_relative_path_with_the_source_text_as_payload(self):
        with tempfile.TemporaryDirectory() as tmp:
            shaders = Path(tmp) / "shaders"
            (shaders / "opengl").mkdir(parents=True)
            (shaders / "opengl" / "default.shader.glsl").write_text("void main() {}")
            out = Path(tmp) / "f3d.o2r"

            self.assertEqual(build_f3d_o2r(shaders, out), 1)

            with zipfile.ZipFile(out) as z:
                self.assertEqual(z.namelist(), ["shaders/opengl/default.shader.glsl"])
                data = z.read("shaders/opengl/default.shader.glsl")
            header = unpack_resource_header(data)
            self.assertEqual(header.resource_type, 0x53484144)
            self.assertEqual(data[OTR_HEADER_SIZE:], b"void main() {}")

    def test_rejects_an_empty_shader_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(FileNotFoundError):
                build_f3d_o2r(Path(tmp), Path(tmp) / "f3d.o2r")


if __name__ == "__main__":
    unittest.main()
