# Monitor animation lists

Written by `tools/o2r_packer/pack_monitor_lists.py` into the objects archive at
`monitor/lists` (`OBLB`, ASCII text). It is what the decomp's `propobj.c` shows
on the screens of monitors: command lists that scroll, zoom, recolour and swap
the image of a screen, and the table that turns the number a stage's setup
gives a monitor into one of them.

One tab-separated line per record:

| Kind | Fields after the kind |
|---|---|
| `list` | list name, then the list's words |
| `default` | the list a number with no `image` line gets |
| `image` | number, list name |

A word is an unsigned decimal, or `@<name>` for the address of another list.
Lists are in source order, so a list's index is the number of `list` lines
before it.

A command is its first word, its type, and the words after it:

| Type | Name | Words after the type |
|---|---|---|
| 0 | stop scrolling | none |
| 1 | scroll relative in x | amount (1/1024 of the image, signed), frames |
| 2 | scroll relative in y | amount, frames |
| 3 | scroll to x | position, frames |
| 4 | scroll to y | position, frames |
| 5 | zoom width | scale (1/1024), frames |
| 6 | zoom height | scale, frames |
| 7 | use image | image number in the monitor image table |
| 8 | hold | frames |
| 9 | jump | list |
| 10 | jump on chance | list, threshold; taken when the high half-word of a random number is below it |
| 11 | restart | none |
| 12 | stop | none (the list never advances past it) |
| 13 | colour | red, green, blue and alpha as one word, frames |
| 14 | set rotation | angle (65536 is a full turn) |
| 15 | rotate | angle per frame |

A command that is not taken (a chance that fails) goes on to the next one.
