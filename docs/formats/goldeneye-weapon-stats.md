# Weapon table

Written by `tools/o2r_packer/pack_weapon_stats.py` into the objects archive at
`weapon/stats` (`OBLB`, ASCII text). It holds the fields of the decomp's
`WeaponStats` structs that the level code reads, resolved through the item
table (`gitem_structs`) so every `ITEM_IDS` value has a line. An item flagged
as having no model takes the defaults (`default_weaponstats`).

Tab-separated lines, items first in `ITEM_IDS` order, then ammo types:

`item` lines:

| Field | Meaning |
|---|---|
| index | The `ITEM_IDS` value, from 0 (`ITEM_UNARMED`) |
| name | The `ITEM_*` identifier |
| hasHeader | 1 if the item's record has a model header (`get_ptr_weapon_model_header_line` is non-null) |
| ammoType | `AmmoType`: the `AMMOTYPE` value, 0 for none |
| magSize | `MagSize`: rounds before a reload |
| flags | `BitFlags`, the `WEAPONSTATBITFLAG_*` bits, in hex |
| model | The resource base path of the model the item holds (`models/gun/G<name>Z`; `models/chr/C<name>Z` for the suit's hand), `-` for a record with no model file |
| skeleton | The model header's `SKELETON(<name>)`, `-` when the model has none |
| muzzleFlashExtension, posX, posY, posZ | `MuzzleFlashExtension` and the on-screen position `PosX`, `PosY`, `PosZ` |
| playX, playY, playZ | `PlayX`, `PlayY`, `PlayZ`: how far the gun follows the crosshair |
| crosshairSpeed, aimLockSpeed | `CrosshairSpeed` and `AimLockSpeed`, the aim damping |
| sway | `Sway`, the scale of the hand's sway |
| boltRecoilBack | `BoltRecoilBack`, how far the slide travels |
| automaticFiringRate | `AutomaticFiringRate` as a signed byte: ticks between the shots of a held trigger, -1 for none |
| singleFiringRate | `SingleFiringRate` as a signed byte: extra ticks after a single shot's recoil, negative for none |
| recoil0 to recoil3 | The four signed bytes of `RecoilSpeed` (a union with `b44[4]`), high byte first: ticks of the recoil's kick and settle, then the two for a held trigger's second recoil |
| recoilBack, recoilUp | `RecoilBack` and `RecoilUp`: how far and how many degrees the shot throws the gun |
| cartridge | The resource base path of the casing model the gun ejects (`models/gun/G<name>Z`), `-` for none |
| loudnessMin, loudnessMax | `LoudnessMin` and `LoudnessMax`: the noise a shot makes, at least and at most |
| noiseIncrease, noiseDecayLinear, noiseDecayScaled | `NoiseIncreasePerShot`, `NoiseDecayLinearTime` and `NoiseDecayScaledTime` |

The floats are decimal text as the decomp writes them (`SCALEVAL_*` constants
and `RATE_*` rates resolved to their original-release value).

`ammo` lines (the `ammo_related` table):

| Field | Meaning |
|---|---|
| index | The `AMMOTYPE` value |
| name | The `AMMO_*` identifier, `-` if the enum names no such type |
| maxAmmo | `MaxAmmo`: the most the player can carry |
| iconImage | `IconImage`: the ammo icon's entry in the global image table, as its offset in hex (`0x0` for none); its list is `hud/img/<offset>` (`goldeneye-hud-images.md`) |
| iconYOffset | `IconYOffset`: how far the HUD moves the icon vertically |

The packer refuses to write if the item enum and the item table disagree on the
item count, or an item names stats that are not defined.
