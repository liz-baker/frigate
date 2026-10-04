# Briefing files

Written by `tools/o2r_packer/pack_geometry.py` into the geometry archive as
`obseg/brief/<name>` (`OBLB`), one per row of the decomp's
`scripts/filelist.u.csv` under `assets/obseg/brief/` (for example
`obseg/brief/UbriefdamZ`). The payload is the file after decompression,
big-endian, 48 bytes. Read by `src/game/menu/BriefingModel.cpp`.

| Offset | Field |
|---|---|
| 0 | four text ids, big-endian u16 each: the Background, M briefing, Q branch and Moneypenny pages |
| 8 | ten objectives, 4 bytes each |

An objective is a big-endian u16 text id followed by a big-endian u16 lowest
difficulty it is shown on (0 Agent, 1 Secret Agent, 2 00 Agent). A text id of 0
ends the list.

A text id is `bank * 0x400 + slot`: the bank is an index of the text banks
(`obseg/text/L<name>E`, `src/level/Lang.h`) and the slot is the string within
it. The stage's own bank is the one these ids name.
