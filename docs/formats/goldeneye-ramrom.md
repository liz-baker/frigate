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
| 0x28 | 2 | The `options` field of the save file at 0x1C: the settings the player had when the demo was recorded, which the stage load copies into the game's options (`fileLoadSettingsForFolder`). Bit 0 inverts the look, 1 auto aim, 2 toggles aim, 3 shows the sight, 4 looks ahead, 5 shows the ammo, 6 is the wide screen, 7 the 16:9 ratio, 8 to 10 the control style (`ControlConfig`), and 11 the cinema screen |
| 0x7C | 4 | totaltime_ms: playback fades to the title 60 ticks before this |
| 0x80 | 4 | filesize |
| 0x84 | 4 | mode (`GAMEMODE`): 0 for a solo stage |
| 0x88 | 4 | slotnum: selects the stage setup's records for this demo (`goldeneye-stage-setup.md`); 0 to 3 |
| 0x8C | 4 | numplayers (`selected_num_players`) |
| 0x90 | 4 | scenario (`MPSCENARIOS` row) |
| 0x94 | 4 | mpstage_sel (`MP_stage_selected`) |
| 0x98 | 4 | gamelength (`game_length` row) |
| 0x9C | 4 | mp_weapon_set |
| 0xA0 | 16 | mp_char: a character row per player, -1 for the player's own |
| 0xB0 | 16 | mp_handi: a handicap row per player |
| 0xC0 | 16 | mp_contstyle: a control style per player |
| 0xD0 | 4 | aim_option (`aim_sight_adjustment` row) |
| 0xD4 | 16 | mp_flags: each player's team flag |

Before the stage loads the game restores `randomseed` and `randomizer` (the
guards' generator) into its own state, along with the multiplayer settings the
header carries. A solo stage takes its control style from the save file's options, not from the
multiplayer settings. The rest of the header holds a copy of
a save file (0x1C to 0x7C) and those multiplayer settings.

The header is 0xE8 bytes, then one packet per frame:

| Size | Field |
|---|---|
| 1 | speedframes: the frame's clock ticks |
| 1 | count: samples in the frame |
| 1 | randseed: low byte of the random state after the frame |
| 1 | check: byte sum of the three fields above and every sample byte |
| 4 * size_cmds * count | samples: `s8` stick x, `s8` stick y, `u8` buttons low, `u8` buttons high, per controller |

The stream ends at a packet with `count` and `speedframes` both 0.

None of the 14 attract demos records a multiplayer game: all have mode 0 and
numplayers 0, and their multiplayer fields hold the front-end's untouched
defaults (Temple, 10 minutes, weapon set 11, aim row 3, characters -1, zero
handicaps, styles and flags). Some record two or three controllers' input (`size_cmds`), which
is not a second player.
