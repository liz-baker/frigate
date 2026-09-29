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

The floats are decimal text as the decomp writes them (`SCALEVAL_*` constants
resolved to their original-release value).

`ammo` lines (the `ammo_related` table):

| Field | Meaning |
|---|---|
| index | The `AMMOTYPE` value |
| name | The `AMMO_*` identifier, `-` if the enum names no such type |
| maxAmmo | `MaxAmmo`: the most the player can carry |

The packer refuses to write if the item enum and the item table disagree on the
item count, or an item names stats that are not defined.
