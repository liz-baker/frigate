# Split-screen rendering

A stage of two to four players draws one view per player on the 320 by 240
screen, in the order and with the layouts of the GoldenEye decomp. The code is
`Stage::Render` (lvlRender), `level/ScreenLayout.h` (the viewport and z-buffer
functions of `bondview.c` and `viewport.c`) and `level/ScreenSetup.h` (what
each pass draws for its share of the screen: `fr.c`'s
`viSetupScreensForNumPlayers`, the z-buffer clear and the color image).

## Layouts

Every layout is in the 320 by 240 screen the stage draws on, whatever the
display's shape; the two-player and quadrant views have one size regardless of
the screen setting (wide screen, cinema), which only a single player's view
follows.

| Players | Player 1 | Player 2 | Player 3 | Player 4 |
|---|---|---|---|---|
| 1 | 0, 10, 320 x 220 (wide screen: 0, 30, 320 x 180; cinema: 0, 52, 320 x 136) | | | |
| 2 | 0, 10, 320 x 109 | 0, 121, 320 x 109 | | |
| 3 | 0, 10, 159 x 109 | 161, 10, 159 x 109 | 0, 121, 159 x 109 | the quadrant stays black |
| 4 | 0, 10, 159 x 109 | 161, 10, 159 x 109 | 0, 121, 159 x 109 | 161, 121, 159 x 109 |

(left, top, width x height.) The player's projection aspect is the shape of its
own view: 320/109 for two players, 159/109 for three or four.

The first player drawn in a frame paints the black between and around the
views (`FillUncoveredScreen`): rows 0 to 9 above, the rows below the lower
views, rows 119 and 120 between the two halves and, for three or more players,
columns 159 and 160 between the columns. A game of three also fills the
empty fourth quadrant. The frame is cleared to black before the first pass as
well, so a bar the decomp leaves to whatever the last frame held (when the
first player drawn has a lower view) is black here.

## The z-buffer

The decomp's z-buffer for two or more players is half the screen high (120
rows), shared by the upper and lower view of a column; `zbufInit` moves the
depth image back 120 rows for the lower views and `zbufClearCurrentPlayer`
clears the player's column. The interpreter's depth buffer is the whole screen,
so each pass clears the same rows of it that the decomp's player does of its
half, in screen rows: the upper views clear rows 0 to 119 and the lower views
rows 120 to 239, in the player's own half of the width for three or four
players. No view's clear touches another view, so the order the passes run in
does not matter. A single player clears the whole screen, which the interpreter
takes as the depth clear it has already done.

`resolution != 0`, the 440 by 330 mode of Cuba's camera feeds, makes the
z-buffer 440 by 330 whatever the player count. `ZBufferAllocation` and
`ZBufferClearRect` take it; nothing sets it until the camera feeds are ported
(issue 136), and Cuba has no multiplayer.

## A frame

`Stage::Render` runs lvlRender's loop: for each player in the shuffled order it
makes that player current, builds their pose (`CameraPose`) and draws one pass:

1. The frame's pipeline setup, the z-buffer clear, the viewport and projection
   of the player's view, and the framebuffer as the color image.
2. The camera matrices, the screen shake and the black between the views.
3. The sky, the rooms' visibility, the props' tick and on-screen list, the
   auto-aim target, the player's pickups, the rooms and props, the effects.
4. The guns, the watch, the player's death sequence and the HUD, then the
   colour wash and blood, all inside the player's view.

The state a frame does once is gated on the pass of the player drawn first
(`get_player_position_in_shuffled == 0`): the room cache's ageing, the alarm,
the AI's sounds, the room lists' defragmentation, the weapons' fuses
(`chrobjWeaponTick`) and the objects' shared simulation (`objTick`'s
`isSimOwner`: a projectile's flight, the doors, vehicles and autoguns, which a
projectile runs in the pass of the player who fired it). The video interface's
shake advances once, after the last pass.

The per-frame stores that display list commands point into (the sky's and the
props' vertices and matrices, the gun's, the effects') start over with the first
pass and are added to by the others, since the frame's list runs only once every
view has been built. Each pass starts from the frame's pipeline setup rather than
the state the previous player's HUD left.

A display list for four players has room for four times the one-player
frame (16384 commands each): the decomp's buffers grow with the player count
for the same reason.

## Aspect ratio

Libultraship widens a 3D view to the display by scaling vertex x by
(4/3) / (display aspect) and stretches the viewport and scissor rectangles of the
320 by 240 screen across the whole framebuffer. 2D (texture rectangles, fills,
text, so the HUD) is also scaled to a 4:3 frame, but in the middle of the
display. For a single player that lines up: the view fills the display and the
HUD is centred over it.

- **One and two players** (full-width views) follow the Aspect Ratio setting like
  single-player live play: the views stretch across the display, the projection
  aspect is the native shape of the view (320/109) and the culling and room
  scissors widen by the display's factor, so each view's world matches its
  pixels on any display. The HUD stays in the 4:3 middle, as in one-player play.
- **Three and four players** (quadrants) are drawn at 4:3, pillarboxed by
  libultraship, whatever the setting. Stretched across a wide display the
  quadrants would sit at their native columns of the full width, while the HUD
  of each (crosshair, ammo, text) is placed in the 4:3 middle: the crosshair
  would no longer be at the centre of its view. At 4:3 the 2D layer and the views
  agree, and each view's aspect is its pixel shape exactly.
- **4:3 displays** change nothing: the factor is 1 and a view's aspect is its
  native shape.
- **Hi-res** (the 440 by 330 mode) is Cuba's camera feeds in a single-player
  game; split screens never use it. Menus, demos and cutscenes are 4:3 as
  before.

`Display::WidensSplitScreen(playerCount)` makes the choice; the game loop and the
level viewer apply it with `SetLivePlay`.

## The HUD

Each pass draws its player's HUD inside the player's view (`Hud::Render`), with
the layouts of the GoldenEye decomp for the player count and number. Positions
are relative to the view's rectangle:

| | 1 player | 2 players | 3 or 4 players |
|---|---|---|---|
| Bottom message, left edge | 30 | 30 | 30 (left column), 10 (right column) |
| Bottom message, bottom edge | 12 above the view's bottom, 40 with a left-hand weapon with ammo or a mission clock | the same, and 8 higher for player 2 | 16 below the view's top |
| Ammo counter, in from the left / right edge | 59 / 59 | 59 / 59 | 59 / 109 (left column), 43 / 127 (right column) |
| Radar centre | | 41 in from the right edge, 26 below the top | the same, 15 further right in the left column |

A multiplayer message strip holds one message (a new one replaces it and
restarts its 120 ticks), unlike a solo game's queue of five. The mission clock
and the top message strip keep their solo positions (the strip's fill runs the
width of the screen, in every view); in a security camera view (Cuba's feeds)
the top strip moves to 70 in from the view's left and snaps to the 11 pixel
rows (`cameraBufferToggle`; nothing sets it until the camera feeds, issue 136).

The radar (`level/Radar.h`) shows a dot for every other living player, 16 pixels
out at 4000 world units and on the rim beyond, turned with the viewing player's
heading. Dots are yellow, or in the team scenarios, Flag Tag and the Golden Gun
red for a player without the item (or on the first team) and blue for one with it
(or on the second). It is hidden for a dead player, a player whose menu is open
and when the no-radar cheat is on. Its circle is image `0x1020` of the global
image table (`docs/formats/goldeneye-hud-images.md`).

A player's open menu (`PlayerState::mpMenuOn`) shades their view and hides their
message strips, sight, ammo counter and radar, and shows the gauge bars; those
bars also show while `healthdisplaytime` runs, unless the game is over. The menu
itself is below.

The 2D HUD is in the 4:3 middle of a wider display and the three and four
player views are pillarboxed to 4:3 (see Aspect ratio), so the layouts hold in
every shape.

## The menu in a view

An open pause or watch menu is drawn inside its player's view, in the 160 pixel
column the original lays it out in: a view of two players is twice that wide, so
its text is moved 80 pixels right, and a quadrant is exactly one column. The
view is dimmed behind it (see the HUD above), the text uses the gothic font at its 320 by 240 size,
and the three and four player views place the weapon of choice and awards 7
pixels either side of the column (alternating with the player). The lines come
from `PlanMpWatch` (`level/MpWatchMenu.h`), which is plain data in and out, and
`DrawMpWatchPlan` draws them. A dead player's prompt is centred in their view
instead.

## Not here

The player bodies other views see are owned by issue 391; audio and rumble
are in [audio.md](audio.md#in-multiplayer).

## Checking it

`just mp_test viewports passes` asserts, at 2, 3 and 4 players, each player's
view rectangle and projection aspect and that a frame's passes (in shuffled
order) each set that player's viewport, own projection and one z-buffer clear
and look out of that player's own camera, and `hud-layout`, `hud-radar-dead`, `hud-menu`
and `hud-messages` that each player's pass has its message, ammo icon, sight and
radar where the layouts above put them. `ScreenLayoutTest` and
`ScreenSetupTest` check the layouts, the z-buffer rectangles and the black
fills against the decomp's arithmetic. For a look, `just multiplayer 2` (or 3,
4) opens the game; see docs/multiplayer-verification.md.
