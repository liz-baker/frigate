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
| 0x18 | 4 | size_cmds: controllers recorded |
| 0x7C | 4 | totaltime_ms: playback fades to the title 60 ticks before this |
| 0x88 | 4 | slotnum: selects the stage setup's records for this demo (`goldeneye-stage-setup.md`); 0 to 3 |
| 0xC0 | 4 | mp_contstyle[0]: the first player's control style (`ControlConfig`) |

Before the stage loads the game restores `randomseed` and `randomizer` (the
guards' generator) into its own state, along with the control style and the
multiplayer settings the header carries. The rest of the header holds a copy of
a save file (0x1C to 0x7C) and those multiplayer settings.

The header is 0xE8 bytes (save data and multiplayer settings fill the rest),
then one packet per frame:

| Size | Field |
|---|---|
| 1 | speedframes: the frame's clock ticks |
| 1 | count: samples in the frame |
| 1 | randseed: low byte of the random state after the frame |
| 1 | check: byte sum of the three fields above and every sample byte |
| 4 * size_cmds * count | samples: `s8` stick x, `s8` stick y, `u8` buttons low, `u8` buttons high, per controller |

The stream ends at a packet with `count` and `speedframes` both 0.
