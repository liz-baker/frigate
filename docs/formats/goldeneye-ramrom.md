# Attract-mode demos

Written by `tools/o2r_packer/pack_ramrom.py` into the objects archive.

- `ramrom/table` (`OBLB`, ASCII text): the decomp's `ramrom_table`, one line
  per demo in table order, `<name>\t<locked>`. `select_ramrom_to_play` picks
  from the leading entries whose `locked` is at or below the highest stage
  unlocked.
- `ramrom/<name>` (`OBLB`): the demo's recorded file, byte for byte from
  `assets/ramrom/ramrom_<name>.bin`. Big-endian.

The file starts with the decomp's `ramromfilestructure`; the fields the stage
load reads:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 8 | randomseed |
| 0x08 | 8 | randomizer |
| 0x10 | 4 | stagenum (`LEVELID`) |
| 0x14 | 4 | difficulty (`DIFFICULTY`) |

The remaining header fields (`size_cmds`, save data, multiplayer settings) and
the packet stream are read by ramrom playback.
