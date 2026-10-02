#!/usr/bin/env python3
"""Unit tests for pack_monitor_lists.py against a synthetic propobj.c."""

import tempfile
import unittest
import zipfile
from pathlib import Path

from o2r_writer import OTR_HEADER_SIZE, O2rWriter
import pack_monitor_lists as p

HEADER = """
/* Loop: restarts */
#define MONHORZSCROLL(x,t) 0x1, x, t
#define MONUSEIMAGE(x) 0x7, x
#define MONHOLDTIME(t) 0x8, t
#define MONJUMPTO(p) 0x9, p
#define MONJUMPCHANCE(p,x) 0xA, p, x
#define MONLOOP() 0xB
#define MONRGBA(rgba, t) 0xD, rgba, t
#define MONZOOMWIDTH(x,t) 0x5, x, t
#define MONZOOMHEIGHT(y,t) 0x6, y, t
#define MONZOOMSQUARE(w,t) MONZOOMWIDTH(w, t), MONZOOMHEIGHT(w, t)
#define COLOR_BLACK 0x000000FF
#define TEN_PERCENT_CHANCE 0x1999
"""

TEXTURES = "enum MONIMGIDS {\n    IMGBOND, //0x0\n    IMGSTATIC,\n    IMGTEXT\n};\n"

PROPOBJ = """
u32 monAnim00Bond[] = {
    MONUSEIMAGE(IMGSTATIC),
    MONHORZSCROLL(0xFFFFFD80, 20),
    MONRGBA(COLOR_BLACK, 20),
    MONZOOMSQUARE(0x200, 30),
    MONHOLDTIME(20),
    MONLOOP()
};
// a comment
u32 monAnimRandom[] = {
    MONJUMPCHANCE(monAnim00Bond, TEN_PERCENT_CHANCE),
    MONJUMPTO(monAnimRandom)
};
void monitorSetImageByNum(MonitorRecord *mon, s32 monAnimID)
{
    s32 *image = &monAnim00Bond;
    switch (monAnimID)
    {
         default:
         case 0:
            break;
        case 1:
            image = &monAnimRandom;
            break;
    }
}
"""


class PackMonitorListsTest(unittest.TestCase):
    def test_a_list_is_its_expanded_words(self):
        lines = p.build_monitor_lists(PROPOBJ, HEADER, TEXTURES).decode().splitlines()
        first = lines[0].split("\t")
        self.assertEqual(first[:2], ["list", "monAnim00Bond"])
        self.assertEqual(
            first[2:],
            ["7", "1", "1", "4294966656", "20", "13", "255", "20", "5", "512", "30", "6", "512", "30", "8", "20", "11"],
        )

    def test_a_list_can_name_another(self):
        lines = p.build_monitor_lists(PROPOBJ, HEADER, TEXTURES).decode().splitlines()
        self.assertEqual(lines[1].split("\t")[2:], ["10", "@monAnim00Bond", "6553", "9", "@monAnimRandom"])

    def test_the_image_numbers_map_to_lists(self):
        lines = p.build_monitor_lists(PROPOBJ, HEADER, TEXTURES).decode().splitlines()
        self.assertIn("default\tmonAnim00Bond", lines)
        self.assertIn("image\t1\tmonAnimRandom", lines)
        self.assertNotIn("image\t0\tmonAnim00Bond", lines)

    def test_an_unknown_token_is_refused(self):
        with self.assertRaises(p.MonitorListsError):
            p.build_monitor_lists(PROPOBJ.replace("IMGSTATIC", "IMGNOPE"), HEADER, TEXTURES)

    def test_the_resource_is_written(self):
        with tempfile.TemporaryDirectory() as tmp:
            decomp = Path(tmp) / "007"
            (decomp / "src" / "game").mkdir(parents=True)
            (decomp / "assets").mkdir()
            (decomp / "src" / "game" / "propobj.c").write_text(PROPOBJ)
            (decomp / "src" / "game" / "chrai.h").write_text(HEADER)
            (decomp / "assets" / "oddtextures.h").write_text(TEXTURES)
            out = Path(tmp) / "out.o2r"
            with O2rWriter(out) as writer:
                self.assertEqual(p.pack_monitor_lists(decomp, writer), "monitor/lists")
            with zipfile.ZipFile(out) as archive:
                data = archive.read("monitor/lists")
            self.assertTrue(data[OTR_HEADER_SIZE:].endswith(p.build_monitor_lists(PROPOBJ, HEADER, TEXTURES)))


if __name__ == "__main__":
    unittest.main()
