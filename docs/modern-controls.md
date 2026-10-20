# Modern controls

How a port in Modern mode drives live play. The bindings it reads are the
store's (`docs/bindings.md`); the menu side is in `docs/menu.md`. Modern is the
one deliberate departure from the decomp's input: attract demos replay recordings
and never see it, N64 mode and ports on Default bindings read libultraship's
mappings as before, and everything the decomp does with the input (aim,
auto-aim, zoom, lean, tank, the watch) is unchanged.

A port is in Modern mode when its device is Keyboard & Mouse or a gamepad (not
Default bindings or None), the device is attached, and the port's mode is
Modern. A fresh install gives port 1 a device on its own (Keyboard & Mouse, else the
first gamepad; `docs/menu.md`, Devices), so a new player starts in Modern mode;
a player with a saved device or N64 mappings of their own keeps what they have.
Each such port drives its own player; the two-controller control styles
(2.x) are not supported there, and a Modern player never reads a second pad.

## Pipeline

`Platform::Input::Poll`, once per displayed frame:

1. `ReadPads` fills libultraship's pads. A Modern gamepad port keeps only its two
   stick mappings, so `stick_x/y` and `right_stick_x/y` are the sticks after the
   port's dead zone, sensitivity and notch snap settings.
2. `ReadDeviceFrame` (`BindingsApply.cpp`) reads the port's device into a
   `DeviceFrame`: keys, mouse buttons and gamepad buttons held, the axes
   (sticks from step 1, triggers raw), the wheel notches the deck read this frame
   and, while the mouse is captured, its motion in pixels. Keyboard and mouse
   inputs come from ImGui's state and are dropped while the ImGui menu has the
   keyboard or the pointer (`ControlDeck::KeyboardGameInputBlocked`,
   `MouseGameInputBlocked`). Nothing here uses SDL types outside
   `BindingsApply.cpp`.
3. `ModernSynth::Step` (`ModernInput.cpp`, pure, unit-tested) turns the port's
   Modern profile, the frame, a `ModernContext` and the port's `ModernSettings`
   into a `ModernOutput`: a `PadSample` for `Current()` and a `ModernFrame`.
4. `Stage::TickLive` hands the frames to `BuildPlayerMoveInput`, which fills
   `MoveInput::modern` and the fields below for a player whose port is active.

The context comes from the stage through `SetModernContextProvider` (set by the
game loop; the level viewer sets its own): `menu` when the watch, a multiplayer
menu or the pause has the controls, or no stage is running (front-end screens,
mission end, attract demos); otherwise whether the player is aiming, holds a gun
that zooms (`weaponDisablesCrouch`, the decomp's gate on zoom) and is in a tank.
The context is last tick's state, so the synthesis treats the aim key going down
as aiming already.

## What each action does

Actions are live in the contexts the store gives them, with the exceptions in the
last two lines.

| Action | Result |
|---|---|
| Move, Strafe | `MoveInput::moveX/moveY`, stick units (75 at full). Keys are normalized so a diagonal is no faster; a stick keeps its analog value. On foot `PlayerMove` reads `moveY` as the walk stick and `moveX` as the analog strafe the 2.x styles use; in a tank `moveX` turns the hull, `moveY` drives. Off while aiming: the decomp lets nobody walk in sight-aim. |
| Look, Turn | Direct angle path, see below. |
| Aim | L and R on the pad (control style 1.1's aim), so the game's own aim toggle option, crosshair and auto-aim run unchanged. |
| Fire | Z. |
| Crouch | `crouchHeld` (the key's level) with the port's hold/toggle setting (`crouchMode`, #499). |
| Interact, Reload | `interactPressed`, `reloadPressed`: edges, one frame (#498). |
| Next, Previous Weapon | A, and A with Z. A wheel notch is a one-frame press with a frame between (a fast spin queues, at most eight); a held button is a level, so North held with fire pressed is the decomp's "A held then fire". |
| Zoom | While aiming with a scope: D-pad up and down, which style 1.1 reads as zoom. A wheel notch is six frames of it; the left stick past half is a level. |
| Lean | While aiming, not in a tank: D-pad left and right, which style 1.1 reads as lean. |
| Detonate | The A+B chord as buttons, plus `MoveInput::detonate`: without a remote mine in hand `PlayerMove` drops the weapon change and use the chord would otherwise make. |
| Pause | Start, everywhere. |
| Menu actions | In the menu context: the stick (analog for a stick, full for a key), A for Select, B for Back. On the keyboard and mouse, left click is Select and right click is Back by default; the mouse also moves the cursor, see "Mouse in the front end". |

An input held by a live Zoom or Lean binding doesn't also change weapon, so the
wheel zooms while aiming with a scope and changes weapon otherwise, and the left
stick's Y walks on foot and zooms while aiming with a scope.

Modern players read as control style 1.1 whatever their option says (1.3 and 1.4
swap the buttons, 2.x reads a second pad), the B tap is never used (the chord
excepted), and there is no invert-Y: the "look inverted" option is ignored.
Cheat-entry chords are not read; the Dev Tools tab has the cheats.

## Look

`MoveInput::lookYaw/lookPitch` are degrees to turn and pitch this tick, positive
right and up: the mouse delta times `kMouseDegreesPerPixel` (0.07) times the
port's mouse sensitivity. `lookYawRate/lookPitchRate` are -1 to 1 through the
decomp's squared response times the look sensitivity, for a stick: `PlayerMove`
turns them into degrees with `kFullStickDegreesPerTick` (3.5, what a full stick
turns per tick) and the tick time, so a stick turns the same per second at any
frame rate while a mouse delta is the same whatever the tick time. Both are
scaled by the field of view like the stick's turn, so a zoomed view moves less.

`PlayerMove` feeds them through the same state a stick does: the tick's yaw
becomes `speedTheta_` and the pitch `speedVerta_` (verta falls looking up) with
no easing, so the gun swivel that follows the turn, the pitch limits and
`ApplyVertaTheta` are the decomp's. A pitch input stops the slope-following look
ahead, which Modern also leaves off. In a tank the mouse turns the turret (the
hull is `moveX`) and pitches the barrel.

While aiming the mouse and right stick don't turn the view. They move the
sight-aim crosshair the way the stick does in the decomp, where the stick's
deflection is where the crosshair rests and a stick past the threshold scrolls
the view at the decomp's rate: the right stick is that deflection (times the look
sensitivity), and the mouse moves a stick position that stays where it was put
(0.2 stick units per pixel times the mouse sensitivity, held to the stick's
travel) and recentres when aiming ends. The aim stick is `PadSample::stickX/Y`.

## Mouse capture

`SetModernMouseCapture` asks the window to capture the mouse while a Modern
keyboard and mouse port is in live play (`SetAutoCaptureMouse` and
`UpdateMouseCapture`), so the ImGui menu releases it while open and the
front-end screens never capture. F2 still toggles it until the next change.
Without capture the mouse delta reads zero; the first captured frame's backlog
is flushed because the delta is read every frame.

## Mouse in the front end

On a keyboard and mouse port the pointer holds the front-end menu cursor (the
crosshair, `MenuCursor`) as an absolute position, replacing the stick's
integrated movement there. Nothing changes for N64 mode, gamepads, keyboard
navigation or demos, which never see a pointer.

- `ReadDeviceFrame` reports the mouse in window pixels and the window size
  (`DeviceFrame::pointer`), absent while the ImGui menu has the mouse or the
  pointer is outside the window. `MapToScreen` (`MenuPointer.h`) maps it into the
  largest 4:3 rectangle centred in the window, which is where libultraship draws
  the menus whatever the window's shape, as `[0, 1]` across it; a pointer in the
  bars clamps to the nearest edge.
- `MenuPointerTracker` arbitrates per port while the port is in the menu context.
  The pointer takes the cursor when the mouse moves; a direction key or stick
  deflection gives it back, and a mouse that sits still doesn't take it again. A
  click alone doesn't take it. Poll writes the result into the port's
  `PadSample::pointer`, which reaches the screens as `Input::Sample::pointer`.
- The menu state machine's before-tick hook calls
  `FileSelectModel::MoveCursorToPointer`, once per tick before any screen
  runs, so every screen sees the cursor where the pointer is and a click (A)
  runs the decomp's own hit test at that position: folder boxes, mode rows, the
  mission film strip, the tabs, difficulty rows, the options sliders. The
  decomp's screen margin (`ClampCursor`) still applies. The hook skips ticks where
  a stage is running, so demos are untouched.
- The system pointer is hidden while a screen that draws the crosshair
  (anything stepping `MoveCursorWithStick`) is up, through
  `HideSystemPointer`, which marks libultraship's cursor as managed so a moving
  mouse doesn't show it. It comes back when the screen is left (after a few
  frames' grace), the ImGui menu opens or the pointer leaves the window.
- The store's defaults for the keyboard and mouse profile bind left click to Menu
  Select and right click to Menu Back (the menu context only; they stay Fire and
  Aim in play). A profile saved before has to add them.

The Wii U has no mouse and never produces a pointer.

## Wii U

The Wii U has no keyboard and mouse device, so only the controller column
exists: the device frame takes the pad's buttons, ZL and ZR as the triggers and
the sticks libultraship processed. The Wii U code paths in `BindingsApply.cpp`
build but have not run on hardware.
