# Verifying multiplayer

The attract demos check the single-player simulation against recordings; none of
them carries a multiplayer game (see below), so multiplayer has no recording to
compare with. It is checked three ways: the demos still matching after a change
to the per-player code, a headless harness that drives scripted pads through real
stages at 2, 3 and 4 players, and a manual checklist.

## Single-player regression

Every demo still replays: `just attract N --uncapped --no-abort-on-mismatch` for
N from 0 to 13, reading the "random byte matched X of Y packets" line. All match
in full except demo 1, which diverges at about packet 68.

## The headless harness

```sh
just mp_test              # the core cases at 2, 3 and 4 players
just mp_test stages       # every stage at every player count it allows
just mp_test walk-alone   # named cases or groups
```

It is the ctest `MultiplayerHarness` (core cases) and `MultiplayerHarnessStages`,
and runs the same way from `just test`. It needs the packed archives (`just
pack_archives`) and reports a skip without them. There is no window: it makes a
libultraship context without one, starts the stage through `SelectMultiplayerStage` and
`LiveRun::Start` like the game does, and ticks it with `Stage::TickLive`, the call the
game makes each frame, from scripted pads.

`just mp_test stages` also checks the fog record and that the portals of a stage's windows are left alone at every stage and player count.

The harness is `tests/game/MultiplayerHarness.h` (`MultiplayerSession`, `Report`,
`PlayerScript`) and the cases are in `tests/game/MultiplayerHarnessTest.cpp`:

| Case | Asserts |
|---|---|
| `spawn` | The stage has N players at full health in different places, opens on the multiplayer camera, and the clock runs |
| `idle` | Nobody moves with idle pads |
| `walk-alone` | Pushing one player's stick forward moves that player and no other, for each player in turn |
| `turn-alone` | The same for turning |
| `all-at-once` | With every pad pushing at once (even players forward, odd back) each player goes the way their own stick says |
| `control-styles` | Each player plays the control style they chose |
| `fire-alone` | One player's trigger spends that player's ammunition and nobody else's |
| `deterministic` | The same scripts from the same seed end with every player in the same place |
| `render` | A frame of the stage draws |
| `viewports` | Each player's view is their share of the screen (top and bottom for two players, quadrants for three and four), with the projection aspect of its shape, and no two views overlap |
| `passes` | A frame has one pass per player in the shuffled order, each with that player's viewport, own projection and one z-buffer clear, and every player's camera is their own and different from the others |
| `switch-times` | A weapon change lowers and raises in the multiplayer times (12 ticks each, not 16 and 23) |
| `dual-wield` | With every gun available, no player ever holds a gun in each hand |
| `casings` | Firing a pistol ejects no cartridge casing |
| `scorches` | An explosion leaves no scorch on the floor |
| `fog` | The fog and sky are the record for the stage and player count |
| `spawn-pads` | Every player starts on a start pad, 1000 units from the others where the stage has room |
| `kill` | Each player in turn is killed by the next: only they die, and the kill, death, per-opponent kill counts and kill time land on the right players |
| `suicide` | A player's own kill counts as a suicide (and a credit to themselves), not a kill or a death, and drops nothing |
| `drop-inventory` | A killed player's weapons lie where they fell; the killer's surroundings do not change |
| `respawn` | A dead player stays dead until A, Z or Start, then lives at full health on a start pad (away from the others where one is clear) with the intro's inventory and ammunition, nothing collected in the last life, and the life's stats reset; the other players are untouched |
| `yolt` | Killed twice by the same opponents in You Only Live Twice, a player cannot respawn |
| `golden-gun` | Killing the holder of the golden gun in The Man With The Golden Gun is counted for the killer |
| `kill-stats` | Kills in one tick make a streak, their times are recorded, and a hit from behind counts for the attacker and a hit from the front does not |
| `distance` | Distance walked is each player's own |
| `explosion` | A player in the middle of an explosion dies and the player who made it is credited |
| `characters` | Each player is a character in the stage (a viewer prop with a character of its own) with the head and body of the roster row they chose and its height |
| `roster` | All 64 roster rows have a body and a head in the character tables |
| `held-weapons` | A player's character holds the weapon in their hand, and swaps it when the hand changes |
| `bodies-drawn` | Player 1 sees the other players' bodies on screen, and not their own |
| `body-animation` | A body stands, walks and sidesteps in the animations of the firing tables, and its gun flashes when the player fires |
| `shoot-player` | Player 1's shots hit the body of each other player and hurt them |
| `weapon-stock` | The stage's weapons and crates are the selected weapon set's: no setup slot is left unsubstituted, the weapons only the crates give are not made, each crate holds the set's ammunition, every made object respawns, and the objects flagged out at this player count are not made |
| `skip-mask` | The stage loads exactly the objects not flagged "not in multiplayer" or "not on N players" |
| `weapon-pickup` | Pistols set: a player who steps on a weapon takes it and its ammunition and nobody else does; it waits 1200 ticks to respawn, is not taken meanwhile, and is taken again once back |
| `crate-pickup` | A player who steps on an ammunition crate takes its ammunition and nobody else does; the crate respawns |
| `thrown-weapons` | Timed mines set: no mine is lying about, the crate gives each player five, and each throws one |
| `scenario-pickups` | The flag of Flag Tag and the golden gun of The Man With The Golden Gun do not respawn; the other pickups do |
| `time-limit` | The "One minute left" message shows to every player a minute before the limit, the game ends on the tick the clock reaches it with every player on the scores page, and then nothing moves and no hit lands |
| `point-limit` | Reaching the limit stops the pads and damage, the game waits for the dying to finish, then ends; the points and ranks of the leader and the rest follow |
| `rank-order` | Each player scores one less than the last, and every place from first to the count is used, with the rank text for it |
| `ties` | Players level on points share a place and the next is skipped (two tied for first leave third next; two tied for third leave no fourth); a suicide costs a point |
| `teams-2v2`, `teams-3v1`, `teams-2v1` | Players on a team share a score, a kill of the other team scores for it, a teammate's kill costs it a point, tied teams are all first, the lost places read as second, and a team over the limit ends the game |
| `mission-state` | A death starts the death music, which plays on until every dead player's blood has finished |
| `innings` | The longest life and the most kills in one life follow the life being played |
| `menu-opens` | Start opens only that player's scores, and dying closes it |
| `quit-confirm` | The pages left and right, the pause page stopping and resuming the clock, an Exit that asks again (no goes back, yes ends the game), the game over pages, B taking a finish back, and the stage ending once every player has finished |
| `favourite-weapons` | A game's end stores each player's weapon of choice |
| `awards` | The awards a game leaves are the ones the rules choose from what the stage counted, and a streak of four is always the first award |
| `game-over-delay` | The delayed scores count down from 300 before the pages answer |
| `menu-display` | Each player's open menu is drawn in their own view: the title, chevrons, rank and the numbers of the scores, kills and losses pages in the player's corner of the layout (the player's own row highlighted), the pause page and its two states, the exit and its confirm, the view dimmed behind it and no other view touched |
| `menu-game-over` | At the game over every view is dimmed and reads GAME OVER and START TO EXIT in turn, with the chevrons further out; the pages to the left reach the weapon of choice and the two awards, and a finished player's page is blank |
| `menu-prompt` | A dead player whose death has played out is asked to press start in the middle of their view, and nobody else is |
| `menu-guns` | An open menu stops the guns firing and flashing |
| `flag-tag` | Flag Tag: the flag leaves the floor when taken and does not come back; its carrier holds it in the right hand, cannot cycle weapons, leaves body armour alone and counts every tick; killed, the carrier stops counting and the flag lies where they fell for someone else to take up |
| `golden-gun-hold` | The Man With The Golden Gun: the gun does not come back once taken; its holder is the only one flagged, leaves body armour alone, and killing them is counted for the killer and drops the gun for the next holder |
| `licence-to-kill` | License to Kill: every player's handicap is 200 and a scratch (or gas) kills at full health and armour; the same scratch kills no one in the normal scenario |
| `yolt-round` | You Only Live Twice: players killed twice are placed by how many were alive, play stops once all but one are out, and the game ends once they have finished dying, at 2, 3 and 4 players |
| `team-play-2v2`, `team-play-3v1`, `team-play-2v1` | A teammate's hit hurts and kills like an opponent's and is credited the same, and teams survive deaths and respawns |
| `hud-layout`, `hud-layout-2v2`, `-3v1`, `-2v1`, `-flag-tag`, `-golden-gun` | Each player's pass has their message box, right hand ammo icon, sight and radar (the circle, a dot for every other player, in the scenario's colours) where the HUD layouts put them for that player count and number |
| `hud-radar-dead` | A dead player has no radar and is no dot on the others' |
| `hud-menu` | An open menu shades only that player's view and hides their message, sight, ammo icon and radar |
| `hud-messages` | A message goes to its own player's strip only, a second replaces the first, and a kill and a death show on the killer's and the victim's strips |
| `trace` | (group `debug`) prints every player's position as one walks, for working out what a new case should expect |

A case is a function `(Report&, MultiplayerSession&)`:

```cpp
void CheckRespawn(Report& report, MultiplayerSession& game) {
    game.WaitForControl();                       // the multiplayer opening is over
    Scripts scripts;
    scripts[0].Press(0, 30, Level::Buttons::Z);  // player 1's pad, ticks 0 to 29 of this Run
    game.Run(120, scripts);
    report.Check(game.GetStage().PlayerAt(1).health.Health() < 1.0f, "player 2 was hit");
}
```

A dying player's death plays out in the render pass of their view, so a case
that kills a player runs the game with `RunDrawingTheDead`, which draws the view
of every dead player each tick, and presses a pad button to respawn them.

Add it to `kCases` with its group, and an optional function that adjusts the
`MultiplayerSettings` it starts with (scenario, weapon set, teams). Rules and
scenarios that later change what a game does (scores, respawns, the end of a
game) add their assertions the same way, reading the stage through
`GetStage()`.

## Multiplayer recordings

The demo header holds the multiplayer settings after the save data
(`docs/formats/goldeneye-ramrom.md`): game mode, player count, scenario, stage,
game length, weapon set, per-player character, handicap and control style, the aim
setting and the team flags. All 14 attract demos are solo stages (game mode 0,
one controller or more recorded for the 2.x control styles) and the multiplayer
fields hold the front-end's untouched defaults (Temple, 10 minutes, weapon set
11, aim 3, characters -1). None records a multiplayer game, so there is nothing
to replay and compare multiplayer against.

## Manual checklist

Run each with `just multiplayer <players> --scenario <name> ...` and give each
player a device (see `docs/multiplayer-quick-start.md`). Play on Temple and one
other stage; for the 3 and 4 player rows repeat on a stage that holds that many
(Bunker, Archives and Caverns hold 3 and Egypt 2).

Every game, at 2, 3 and 4 players:

- [ ] Each player has their own view of the right size, none blank or showing another player's camera
- [ ] Each player's pad drives only their player; the HUD (health, ammunition, weapon) is theirs
- [ ] Players start at different spots and face into the stage
- [ ] Control styles, handicap and character chosen per player take effect on that player only
- [ ] The weapon set is the one chosen; every player has it
- [ ] Shots hurt the player they hit and no one else; a dead player respawns elsewhere
- [ ] The time or point limit ends the game on the scores page; once every player has pressed A, Start or B the game returns to the multiplayer options (to the legal screen for a game started with `just multiplayer`) without a crash
- [ ] Nothing is logged as an error

Per scenario:

| Scenario | Players | Check |
|---|---|---|
| Normal | 2, 3, 4 | A kill scores a point for the killer; a suicide loses one; the limit picks the leader |
| You Only Live Twice | 2, 3, 4 | A player is out after the lives the scenario gives; the last one alive wins; out players cannot respawn |
| The Living Daylights | 2, 3, 4 | The flag can be picked up and held; time or points accrue to the holder only; it drops when its holder dies |
| The Man with the Golden Gun | 2, 3, 4 | One player holds the golden gun; armour is left alone while holding it; killing its holder drops it for the next holder; only the holder scores |
| License to Kill | 2, 3, 4 | Any hit kills; everyone dies in one shot |
| Teams, 2 vs 2 | 4 | The teams match the choice; friendly fire is as the original's; the team score is the sum of its members' |
| Teams, 3 vs 1 | 4 | The lone player is the one chosen; the three share a score |
| Teams, 2 vs 1 | 3 | The pair share a score against the lone player |

The scenarios' play, scores and ends of game are all ported. Also check the HUD: the radar's team colours
(red and blue) in the team scenarios, the message strip and ammo counter in each view.

## Looking at the split screen

The harness has no window, so the picture itself is checked by running the game
under a virtual display with software GL, for example
`LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a just multiplayer 3` and capturing the
window. The layouts, the black between the views and the z-buffer halves are
described in docs/multiplayer-split-screen.md. A software renderer can hide
what a GPU backend shows (the depth clear of each view is the interpreter's
partial depth clear, which only the OpenGL backend implements; the other
backends leave the whole-frame clear to do the work, which is enough since no
two views overlap), so a look on the real backend is still worth having.
