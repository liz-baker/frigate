# EEPROM save image

The player's four folders, as the GoldenEye decomp's `save_data` array
(`file.h`, `file2.c`) keeps them in the cartridge's 4 Kbit EEPROM. The image is
stored unchanged: 512 bytes, big-endian, in `default.sav` in the app
directory (libultraship's `osEepromLongRead`/`osEepromLongWrite`, addressed in
8-byte blocks). Missing, short or damaged data is replaced by blank folders
when the game validates the image (`fileValidateSaves`).

| Offset | Size | Content |
|---|---|---|
| 0x00 | 32 | Header (`smallSave`) |
| 0x20 | 96 | Slot 0 |
| 0x80 | 96 | Slot 1 |
| 0xE0 | 96 | Slot 2 |
| 0x140 | 96 | Slot 3 |
| 0x1A0 | 96 | Slot 4 |

## Header

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | chksum1 |
| 0x04 | 4 | chksum2 |
| 0x08 | 1 | Marker, 0x42 |
| 0x09 | 23 | Zero |

The checksums cover bytes 0x08 to 0x1F. A header with another marker or bad
checksums is rewritten; it doesn't affect the slots.

## Slot (`save_data`)

Four folders share five slots: the fifth is a spare that every update moves
into (`fileOverwriteSaveSlotWithNewSave`), so a write interrupted midway
leaves the old copy intact.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | chksum1 |
| 0x04 | 4 | chksum2 |
| 0x08 | 1 | completion_bitflags |
| 0x09 | 1 | flag_007: bit 0 unlocks 007 mode |
| 0x0A | 1 | music_vol |
| 0x0B | 1 | sfx_vol |
| 0x0C | 2 | options (`OPTION_*`) |
| 0x0E | 3 | unlocked cheats: bit n of the 24 bits, bytes in order, is cheat n (0 to 19) |
| 0x11 | 1 | padding |
| 0x12 | 76 | times |
| 0x5E | 2 | Alignment bytes that the struct copy carries; checksummed |

The checksums cover bytes 0x08 to 0x5F of the slot, alignment bytes included.
A slot that fails them is replaced by a blank save.

### completion_bitflags

| Bits | Mask | Field |
|---|---|---|
| 0-2 | 0x07 | Folder (0 to 3) |
| 3-4 | 0x18 | Update counter, modulo 4 |
| 5-6 | 0x60 | Selected Bond; a new folder stores its own number |
| 7 | 0x80 | Free: the slot holds no folder |

When validation finds two live slots for one folder it keeps the one whose
counter is one ahead of the other's (modulo 4), or the first if neither is.

A blank save has 0x80 here, flag_007 0, both volumes 0xFF, options 0x003A and
everything else zero.

### options

| Mask | Option |
|---|---|
| 0x0001 | Invert look |
| 0x0002 | Auto aim |
| 0x0004 | Aim control |
| 0x0008 | Sight on screen |
| 0x0010 | Look ahead |
| 0x0020 | Display ammo |
| 0x0040 | Wide screen |
| 0x0080 | 16:9 ratio |
| 0x0700 | Control style |
| 0x0800 | Cinema screen |

### times

Best completion times in seconds, 10 bits each, packed from the most
significant bit of byte 0 with no gaps. Entry `(difficulty * 20 + level)` starts
at bit `10 * entry`, with difficulty 0 Agent, 1 Secret Agent, 2 00 Agent and
level 0 to 19 in stage order. 0 means not completed; a stored time of 0 is
written as 0x4F and times above 0x3FF as 0x3FF. 007 mode has no entries (its
time reads as 0x3FF once 007 mode is unlocked). Entries use the first 75 bytes;
the 76th is unused.

## Checksums (`fileGenerateCRC`)

Over a byte range with `polynomial = 0x8F809F473108B3C1` and `shift = 0`: for
each byte forwards, `polynomial += byte << (shift & 0xF)`, then
`chksum1 ^= next(polynomial)` and `shift += 7`; then for each byte backwards,
the same with `chksum2` and `shift += 3`. `next` is `randomGetNextFrom`, the
game's random generator stepping `polynomial`, returning the low 32 bits.

## Not stored here

The demo recordings' folder (`RAMROM_FOLDERNUM`, 0x64) is a sixth slot in
memory only.
