# Gun barrel screen resources

Written by `tools/o2r_packer/pack_eyeintro.py` into the geometry archive.
US ROM only; both ranges are checked against what is found there.

| Path | Type | Contents |
|---|---|---|
| `eyeintro/sight` | `OTEX` v1 | The sniper-sight backdrop: 440x299, 8-bit intensity (`G_IM_FMT_I`, `G_IM_SIZ_8b`), row-major |
| `eyeintro/blood` | `OBLB` | The blood animation stream, 2524 bytes, as the game stores it |

## Backdrop

ROM `0x2A4D50`, 107904 bytes. Big-endian `u16` width (440) and height
(299), six more header bytes, then `(count, value)` byte pairs until
width * height pixels are out. The packer expands it; the runtime never sees
the run-length form.

## Blood animation stream

Initialised data in the game code segment, whose compressed blob (`11 72`
marker, raw DEFLATE; `goldeneye-containers.md`) starts at ROM `0x21990`.
The stream is at offset `0xADA0` of the inflated bytes. `src/game/menu/
BloodAnimation.h` decodes it a frame at a time.
