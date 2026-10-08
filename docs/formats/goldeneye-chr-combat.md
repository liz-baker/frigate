# Characters' combat tables

Written by `tools/o2r_packer/pack_chr_combat.py` into the objects archive at
`chr/combat` (`OBLB`, ASCII text). It is the combat data of the decomp's
`chr.c`: the animations a character plays when it is hit, when it fires, and
when an explosion kills it. Animation names are clips (`animations/<name>`,
`goldeneye-animation-clip-resource.md`); every float is written as the
single-precision value the game holds, so it reads back exactly.

One tab-separated line per record, in this order of kinds:

| Kind | Fields after the kind |
|---|---|
| `struck` | list name, index, animation, flip, end frame, speed, knockback, first thud frame, second thud frame |
| `reaction` | index, hit part, impact puff count, impact puff size, back puff count, back puff size, death list (or `-`), flinch list (or `-`) |
| `firing` | table name, index, animation, then 17 floats |
| `group` | set name, index, table name |
| `multifiring` | group, stance, table name (or `-`), index in it, animation (or `-`), x, y, z |
| `explosiondeath` | index, animation, flip, speed, start frame, the unused frame, thud frame, end frame |
| `explosiongroup` | direction, then the `explosiondeath` indices the direction picks among |
| `impact` | item, `ForceOfImpact` of the item's weapon stats; only items with a non-zero force have a line |

`struck` lists are the `StruckAnim` arrays (`death_chest`, `flinch_gun`,
`death_stagger`, ...) up to their terminator. A list has no entry for the
terminator, so its length is the number of its lines.

`reaction` is `g_HitReactionTable` up to its terminator, in table order. The
game looks a row up by its hit part number, not by its index.

`impact` is the force that throws a character a weapon kills backwards
(`bondwalkItemGetForceOfImpact`).

`firing` is a `weapon_firing_animation_table` array: a table of rows ending in
a terminator, which is not written. The 17 floats are, in order, the length of
the aiming part of the animation (`unk04`), the per-frame turn (unused by the
game and written as the source states it), the angle offset, the start frame,
end frame, shoot start and end frames, recoil start and end frames, aim start
and end frames, the upward and downward limits of the aim, its left and right
limits, and the free arm's fraction of the aim up and down. A table whose last
source row is not a terminator runs on into the table that follows it in the
source, as the game's does in memory.

`multifiring` is `firing_animation_groups` (bondview.c), what the character of a
multiplayer player plays for the other players. There are four groups (a gun
held in one hand, a gun held in both, no gun, two guns) of six stances
(standing, walking, running, sidestepping right, sidestepping left,
crouching), each a table entry of `firing` (table name and index; the
animation is `-`) or a lone animation (table `-`). The three floats are the
animation's speed scale, the frame it loops from and the frame it ends at
(`-1` for none).

`group` names, for each of the 32 equal slices of a turn between the character's heading
and its target, the table of firing animations to pick from. The sets are
`rifle`, `pistol`, `doubles` and their `crouched_` forms.
