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

`ammo` lines (the `ammo_related` table):

| Field | Meaning |
|---|---|
| index | The `AMMOTYPE` value |
| name | The `AMMO_*` identifier, `-` if the enum names no such type |
| maxAmmo | `MaxAmmo`: the most the player can carry |

The packer refuses to write if the item enum and the item table disagree on the
item count, or an item names stats that are not defined.
