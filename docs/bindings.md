# Binding store

What each action is bound to, per port, per device type and per mode. The
store is the source of truth for a port that reads a device of its own
(Keyboard & Mouse or a gamepad); libultraship's per-port mappings for that
device type are generated from it. The model, conflict logic, capture state
machine and persistence are libultraship-free and unit-tested
(`src/platform/Bindings.*`, `src/platform/BindingCapture.*`,
`tests/platform/BindingsTest.cpp`); `src/platform/BindingsApply.*` is the
libultraship side.

## Model

- A **profile** holds the bindings for every action of one mode on one device
  type. A port has four: Modern and N64, each for Keyboard & Mouse and for a
  gamepad. Editing one never touches the others, so switching device or mode
  loses nothing. `BindingStore::Mode(port)` is the port's current mode
  (`BindingMode::N64` until the Modern input lands, frigate#500).
- An **action** (`Action`, `Info(action)`) has a config key, a label, a
  **section** for the editor, the **contexts** it is live in, whether it is
  **required** and whether it takes mouse motion. An action can have several
  bindings.
- A **physical input** (`PhysicalInput`) is a key (libultraship scancode), mouse
  button, wheel step, mouse motion, gamepad button or gamepad axis direction.
  Gamepad buttons are named by position; face buttons are South, East, West and
  North and are labelled by `InputLabel`/`FaceButtonLabel` for the pad's layout:

  | Position | Xbox | Nintendo | PlayStation |
  |---|---|---|---|
  | North | Y | X | Triangle |
  | South | A | B | Cross |
  | East | B | A | Circle |
  | West | X | Y | Square |

  Sticks are positive right and up whatever the platform reports; triggers are
  positive only (on the Wii U they are the ZL and ZR buttons).

### Actions

N64 mode: A, B, Start, L, R, Z, D-pad, C-buttons, and the four directions of the
stick. Start, A, B and the stick directions are required.

Modern mode (the defaults follow frigate#500's table; synthesis into gameplay is
frigate#500): Move Forward/Back, Strafe Left/Right, Look Up/Down, Turn
Left/Right, Aim, Fire, Crouch, Reload / Interact (one action: both are the B
tap), Next/Previous Weapon, Pause, Zoom In/Out, Lean Left/Right, Detonate, and
Menu Up/Down/Left/Right/Select/Back. Pause and the menu actions are required.
Mouse motion binds only to look and turn. Previous Weapon has no controller
default: "hold North and fire" is a chord the Modern input adds. Lean Right
defaults to X on the keyboard rather than E, which Reload / Interact holds
while aiming.

### Contexts and conflicts

Contexts are Menu, On Foot and Aiming (`kContext*`). Two actions conflict only
when they share a context and hold the same input, so the wheel is Next/Previous
Weapon on foot (On Foot) and Zoom while aiming (Aiming), and WASD is movement
(On Foot) and menu navigation (Menu). N64 actions are live in every context.

`BindingStore::Check(target, action, input, slot)` reports what binding would do
without changing anything: `Ok`, `Reserved`, `Invalid`, `AlreadyBound` or
`Conflict` with the conflicting actions, and whether **Replace** (take the input
from them; not allowed when it would leave a required action unbound) and
**Swap** (the conflicting action takes the input being replaced; only for a
single conflict and an existing slot) are possible. `Bind(...)` with no
`ConflictChoice` returns `NeedsDecision` and changes nothing; the editor asks,
then calls again with Swap, Replace or Cancel.

Esc (opens the menu) and F2 (mouse capture) can't be bound (`IsReserved`).
`Unbind` keeps the last binding of a required action.

### Reset

`ResetSection`, `ResetProfile` and `ResetAll` (every port, profile and mode).
The Controls tab's Set Defaults button also resets the store.

## Capture

`Capture` is a state machine the editor drives each frame with the clock and
the inputs held (`Update(nowMs, active)`), so tests run it without a window.
`Begin` takes the port's selected device and refuses (with a hint) for Default
bindings, None and a disconnected device. Only the selected device's inputs
count; others are ignored and `Hint()` says which device the port reads. Esc
cancels from any device, F2 is refused with a hint, and the inputs held when
capture starts only count after they are released, so the click that started it
can't bind itself. `Cancel()` is the Cancel button; the timeout is
`kCaptureTimeoutMs`. The editor passes `mouseOverUi` to `PollRawInputs` while
the pointer is over its own buttons, so pressing Cancel can't bind the click.

The editor draws; it gets everything else from these calls: `Info`,
`SectionsOf`, `ActionsOf`, `InputLabel` (with `System::PortFaceLayout`),
`BindingStore::Get`/`Check`/`Bind`/`Unbind`/`Reset*`, `Capture`,
`System::PollRawInputs` for capture and for the live highlight (an input that is
held now appears in the list), and `System::BindingsChanged()` after any edit.

## Applying to libultraship

`Poll` (`src/platform/Input.cpp`) re-applies when the store's `Generation()`, the
port devices or the attached gamepads change. For a port whose device is
Keyboard & Mouse or a gamepad, in N64 mode, the port's libultraship mappings for
that device type are cleared and rebuilt from the profile with libultraship's
mapping classes (`SDL*`, `WiiU*`, `Keyboard*`, `Mouse*`; gamepads through
`PHYSICAL_DEVICE_TYPE_GAMEPAD`, with the Wii U mappings addressed by the selected
pad's device index). Ports on Default bindings keep libultraship's own mappings,
and ports in Modern mode keep them until frigate#500.

## Persistence

`gPort1Bindings` to `gPort4Bindings`, one string per port (`Serialize` /
`Deserialize`):

```
v1;mode=n64;n64.kbm=n64.a:k45,k18|n64.b:...;n64.pad=...;modern.kbm=...;modern.pad=...
```

Only profiles that were edited are written; a missing profile reads as the
defaults, as does an action a profile leaves out (an action written with
nothing after the colon is deliberately unbound). Input tokens: `k<scancode>`,
`b<button>`, `w<direction>`, `m<axis><sign>`, `p<button>`, `a<axis><sign>`.
Loading drops unknown actions, malformed tokens, reserved inputs and inputs a
profile can't hold, and restores the defaults of a required action left bare.

The first launch with the store (no `gPortNBindings` saved and
`gBindingsMigrated` unset) migrates the mappings libultraship loaded from the
config: each port's keyboard, mouse and gamepad mappings become the N64
profiles (`ReadLegacyBindings`, `ProfileFromLegacy`), unless they equal the
defaults. A device type with no mappings, and required actions left empty, get
the defaults. Edits made later in libultraship's own editor on a port that reads
a device of its own are replaced by the store at the next apply.
