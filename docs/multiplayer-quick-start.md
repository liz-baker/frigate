# Starting a multiplayer game without the menus

The multiplayer menus fill in `MultiplayerSettings` (`FrontState::multiplayer`)
and hand the run-stage menu a stage in `GameMode::Multi`. The quick start does
the same thing from the command line or the Dev Tools tab, so a game of 2, 3 or
4 players is one command away, and it stays a way to skip the menus once they
exist.

```sh
just multiplayer 2                                   # two players, the defaults
just multiplayer 3 --stage facility --scenario normal
just multiplayer 4 --scenario 2v2 --teams ABAB --game-length 10points
```

`just multiplayer <players> [options]` is `just run --multiplayer --players
<players> [options]`; the raw form is `./build/frigate --archive-dir archives
--multiplayer --players 4`. It needs the packed archives like any game run.

## Arguments

Any option left out takes the multiplayer defaults for that many players: Temple,
Normal, 10 minutes, the Rockets weapon set, auto-aim and sight on, each player with
their own character, the normal handicap and control style 1.1.

| Option | Takes | Meaning |
|---|---|---|
| `--multiplayer` | | Start a multiplayer game instead of the title sequence |
| `--players` | 1 to 4 (2 to 4 for a real game) | The player count; default 2 |
| `--stage` | a stage name or row | `random`, `temple`, `complex`, `caves`, `library`, `basement`, `stack`, `facility`, `bunker`, `archives`, `caverns`, `egypt` (rows 0 to 11). `random` picks a stage the player count fits |
| `--scenario` | a name or row | `normal`, `yolt` (You Only Live Twice), `daylights`, `goldengun`, `licence` (License to Kill), `2v2`, `3v1`, `2v1` (rows 0 to 7) |
| `--characters` | one number, or one per player | A row of the character table, 0 to 63; -1 is the player's own row |
| `--handicaps` | one number, or one per player | A row of the handicap table, 0 to 10; 5 is normal |
| `--control-styles` | one style, or one per player | `1.1` to `1.4` and `2.1` to `2.4` |
| `--teams` | one letter per player | `A` or `B` for each player of a team scenario, as in `AABB` |
| `--weapon-set` | a name or row | `slappers`, `pistols`, `knives`, `automatics`, `power`, `snipers`, `grenades`, `remotemines`, `launchers`, `timedmines`, `proxmines`, `rockets`, `lasers`, `goldengun` (rows 0 to 13) |
| `--game-length` | a name or row | `unlimited`, `5min`, `10min`, `20min`, `5points`, `10points`, `20points`, `lastalive` (rows 0 to 7) |
| `--aim` | 0 to 3 | The aim setting's row: auto-aim off or on, sight off or on |
| `--cheats` | a comma-separated list | Cheats switched on as the cheat menu would before the game is picked; see below |

Names ignore case and punctuation (`Golden-Gun` is `goldengun`). Per-player lists
are comma-separated (`--handicaps 5,2,8,5`) and take either one value for everyone
or one per player. The settings go through `MultiplayerSettings::Valid`, so a
scenario the player count does not fit (`2v2` needs 4 players, `3v1` 4, `2v1`
3), a stage that holds fewer players (Bunker, Archives and Caverns 3, Egypt 2)
and any out-of-range row are refused with a message before the window opens.
`--teams` for a 2 vs 2 must name two players for each team, and for a 3 vs 1 or
2 vs 1 one player alone.

## Cheats

`--cheats invincible,noradar,dkmode` switches those cheats on for the game. A
cheat is named by its lowercase name or its id (1 to 34): `mpchars`, `invincible`,
`allguns`, `maxammo`, `debug5`, `deactivateinvincible`, `linemode`, `2xhealth`,
`2xarmor`, `invisible`, `infiniteammo`, `dkmode`, `extraweapons`, `tinybond`,
`paintball`, `10xhealth`, `magnum`, `laser`, `goldengun`, `silverpp7`, `goldpp7`,
`bondphase`, `noradar`, `turbo`, `debugpos`, `fast`, `slow`, `enemyrockets`,
`2xrocket`, `2xgrenade`, `2xrcp90`, `2xthrowingknife`, `2xhuntingknife`, `2xlaser`.

They go the way the cheat menu's choice does (`FrontState::cheatActivated`, with
the screens' cheat variants set), so the stage applies them on its first tick as
`cheatButtonTurnOnCheatForPlayers` does: only the cheats flagged for multiplayer
take effect, a player's own ones (invincibility, the health cheats, bond phase)
for every player, and the game's (DK mode, infinite ammo, no radar, line mode,
paintball, turbo mode) once. The rest, such as all guns, invisibility, tiny Bond
and the weapon cheats, are accepted and ignored, as the game ignores them.

The Dev Tools tab's Cheats header has a checkbox for each.

## Controllers

The players' controller ports (1 to the player count) read as connected whether or
not a controller is attached, so the game runs with none. That is separate from
the Controls tab's "Port N connected" settings, which it does not change.

A forced port with no device reads no input. To drive players:

- By default the keyboard and every gamepad feed port 1 only. With gamepads, give
  each player's port its own gamepad under Controls, Controller ports.
- With only a keyboard (or fewer pads than players), set one port's device to the
  keyboard and the others to None, and swap which port has it to drive each player
  in turn. A device can feed more than one port, so the keyboard on two ports
  moves both players together.

## Dev Tools tab

Dev Tools, Multiplayer game has the same choices as widgets (players, stage,
scenario, game length, weapon set, aim, and per player the character, handicap,
control style and, in a team scenario, the team). Start game drops whatever is
running and boots into that game, forcing the controller ports the same way; it
is disabled with the reason beside it while the settings can't start a game.

## What happens

Quick start boots the game with `Options::multiplayerStart` (or
`RequestMultiplayerStart` from the Dev Tools tab, which restarts the game into it).
It calls `SelectMultiplayerStage` (game mode, `FrontState::multiplayer` and the
stage), enters the run-stage menu, and `LiveRun::Start` validates the settings and
loads the stage for the players. When the game ends it returns to the legal screen,
and the menu's Reset button does the same, dropping the quick start.

The game itself is `FrontState::quickStart`; the multiplayer menus will start theirs
without it and go to the multiplayer end screens instead.
