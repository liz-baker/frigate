# The configuration menu

A full-screen translucent panel drawn over the running game with libultraship's
`Ship::Gui`. Closed on every launch.

| Input | Does |
| --- | --- |
| Esc | Opens and closes the menu (libultraship's own binding) |
| Gamepad Back/Select | Same, while "Controller navigation" is on (default on; CVar `gControlNav`) |
| L1 / R1 | Previous / next tab |
| D-pad, A, B | Move, activate, back, while the menu is open; the game doesn't see the controller then |

The top row has the tabs, a search box (filters widgets by name across every
tab) and quit / reset / close buttons. Reset boots the game again from the legal
screen (a fresh front-end state, any demo or stage dropped); it is disabled in the
viewer modes, which have no game to reset.

## Layout of the code

- `src/menu/MenuModel.h/.cpp`: the registry (tabs, sidebars, widget descriptors),
  search, navigation, and hidden/disabled logic. No ImGui, so it is unit-tested
  (`tests/menu/`).
- `src/menu/MenuDraw.cpp`, `MenuWindow.cpp`, `CVarStore.cpp`, `Menu.cpp`: ImGui
  drawing, the window and its theme, CVar-backed values, and `Install()`,
  called from `main()`.
- `src/menu/Tabs.cpp`: the list of every tab. `src/menu/tabs/`: one file per tab.

## Adding a tab

1. Add `src/menu/tabs/<Name>Tab.cpp` defining `void Register<Name>Tab(Registry&)`.
2. In `src/menu/Tabs.cpp`, declare it and call it from `RegisterTabs()`; tabs
   appear in call order.
3. Add the file to `frigate_menu_model` in `CMakeLists.txt`.

## Registering widgets

```cpp
void RegisterExampleTab(Registry& registry) {
    registry.AddTab("Example");
    registry.AddSidebar("Example", "Audio", /*columns=*/2);

    registry.AddWidget("Example", "Audio", "Music volume", WidgetType::SliderInt)
        .CVar("gMusicVolume")
        .Range(0, 100, 5)
        .Default(100)
        .Format("%d%%")
        .Tooltip("Shown on hover.")
        .Callback([] { /* runs after each change */ });

    registry.AddWidget("Example", "Audio", "Channel mode", WidgetType::Combo)
        .CVar("gChannelMode")
        .Options({ "Mono", "Stereo" })
        .Column(1)
        .DisabledWhen([] { return !AudioReady(); }, "Audio isn't running.");
}
```

`AddTab`/`AddSidebar`/`AddWidget` create what is missing and return the existing
tab or sidebar of that name, so a tab can be spread over several files.

Widget types: `Checkbox`, `SliderInt`, `SliderFloat`, `Combo`, `Button`, `Text`,
`Section` (titled divider), `Header` (collapsible, with children), `Group`
(boxed, with children), `Custom` (calls the function given to `.Draw()`, which
may use ImGui; the tab's file then includes ImGui itself).

Builder methods: `CVar`, `Options`, `Range`, `Default`, `Format`, `Tooltip`,
`Column`, `DefaultOpen`, `Callback`, `PreFunc`, `Draw`, `HiddenWhen`,
`Disabled(reason)`, `DisabledWhen(predicate, reason)`, and `Child(name, type)`
for what goes inside a `Header` or `Group`.

- Values with a CVar live in libultraship's console variables and persist in
  the config file. Without one, the widget keeps the value itself.
- `PreFunc(fn)` runs every frame before drawing and may set `hidden`,
  `disabled` and `disabledReason` on the widget. Hidden/disabled state is
  recomputed every frame, so nothing latches. A disabled `Header`/`Group`
  disables its children, with its reason.
- A disabled widget is drawn greyed out and shows its reason as a tooltip.
- Hidden widgets are not drawn and not searchable. Tabs and sidebars have
  `hiddenWhen` too.

The General sidebar in `src/menu/tabs/SettingsTab.cpp` and the example tab in
`tests/menu/MenuModelTest.cpp` show the widget types in use.

## Controls

The Controls tab is our own ImGui editor over the binding store
(`docs/bindings.md`); libultraship's input editor is not used. It draws the same
on every platform, the Wii U included, because it only goes through `System`
and the store. `Install()` hands the tab its draw function through
`SetControlsEditorDraw()`; without one (unit tests) the tab shows a short "not
available" message.

Code: `src/menu/ControlsEditor.cpp` draws; `ControlsModel.*` decides what each
port's tab shows and what is enabled; `BindingFlow.*` sequences capture, the
conflict question and Bind all. The last two have no ImGui and are unit-tested
in `tests/menu/ControlsEditorTest.cpp`.

At the top of the sidebar, a notice says what was connected or disconnected
after the attached devices changed, and which ports lost, regained or were
renumbered onto a different identical gamepad, until dismissed. Under it are
four port tabs. Each holds, in order:

1. **Connected** (`gPort1Connected` to `gPort4Connected`, the `ForcedPorts()`
   bits) and the player count: the connected ports from port 1 up to the first
   gap. A quick-started multiplayer game's ports are forced separately through
   `SessionForcedPorts()`, which the checkbox leaves alone.
2. **Device**: the dropdown, the duplicate-device warning and, when the port
   can't be edited, the reason in orange (no device chosen, None, or the
   device is disconnected).
3. **Mode**: Modern or N64 (`BindingStore::Mode`, saved with the port's
   bindings; Modern by default, except for a port whose libultraship mappings
   were migrated). Switching it never touches the other mode's bindings.
4. **Bindings** for the port's mode and device, in the store's sections. N64:
   A/B/Start/L/R/Z, D-pad, C-buttons and one Control stick section. Modern: the
   store's Modern actions by section, above them the crouch hold/toggle setting
   and the look sensitivity (the mouse on a keyboard and mouse port, the right
   stick on a gamepad port; 0% to 200%, `gPortNMouseSensitivity` and
   `gPortNLookSensitivity`; 100% unset). Each action shows its bindings as buttons (click to
   rebind), an x to remove one (the last binding of a required action stays) and
   a + to add another, up to four. A binding whose input is held now is drawn
   lit. A gamepad port also shows its face buttons as printed on the pad
   (`FaceButtonLabel`, `System::PortFaceLayout`).
5. **Controller settings**, only when the port's device is a gamepad: stick
   sensitivity, deadzone and notch snap angle for each stick (the right stick's
   apply to Modern look, the left stick's to Modern movement), and where the
   platform supports them rumble devices (intensity, Test, remove, add), LED
   devices (color source and color) and gyro (sensitivity, recalibrate). These are
   libultraship's `ControllerStick`, `ControllerRumble`, `ControllerLED` and
   `ControllerGyro` settings, reached through `System`. A keyboard and mouse
   port has none. The Wii U hides LED and gyro, and rumble unless a rumble
   device is already mapped, since adding one works from SDL gamepads only.
6. **Reset to default** (after a confirmation, the current mode's bindings for
   the current device) and **Bind all**.

A port on Default bindings, on None, or whose device is disconnected has its
editor area (bindings and controller settings) disabled with the reason; the
Connected checkbox, device and mode stay usable. Default-bindings ports keep
libultraship's own mappings, so they play with the N64 controls whatever the
mode says: Modern and N64 mode need a device of their own. How a Modern port
plays, and how its mouse drives the game's own front-end screens (not this
menu): `docs/modern-controls.md`. A keyboard and mouse port's Modern profile has
left click on Menu Select and right click on Menu Back by default; the
editor lists them with the other menu actions. The system pointer is shown while
this menu is open.

### Capture

Clicking a binding, + or Bind all opens a modal driven by `BindingFlow` and
`Capture`: the prompt, the capture's hint, the seconds left (`kCaptureTimeoutMs`)
and Cancel. The editor polls `System::PollRawInputs` once a frame and passes
`mouseOverUi` while the pointer is over any item, so a click on the modal's
buttons can't bind; Esc cancels. If the input is already used in the action's
context, the modal asks Swap / Replace / Cancel from the store's `Check`
(Replace is greyed with the reason when it would leave a required action
unbound). Bind all walks every action of the mode in display order: each step
is a normal capture that replaces the action's first binding (others stay), with
Skip and Cancel all. A flow whose port, mode or device changes under it, whose
tab stops being drawn, or that wasn't updated for a second (the menu was
closed) is cancelled.

`gamecontrollerdb.txt` (SDL_GameControllerDB, see `THIRD_PARTY_LICENSES.md`) is
shipped in `assets/` and copied next to the executable at build time;
libultraship loads extra gamepad mappings from it at startup.

### Devices

The dropdown lists Default bindings (the port's bindings as they are: port 1
reads the keyboard, mouse and every gamepad, the other ports nothing), None, one
"Keyboard & Mouse" entry offered only when `ConnectedPhysicalDeviceManager::HasKeyboard()`
and `HasMouse()` are both true (never on the Wii U), and every gamepad from
`GetConnectedGamepads()` (the Wii U lists the GamePad and the Pro Controllers and
Classic-attached Wii Remotes it reports). Choosing a device makes it the only one
the port reads (`ApplyRouting` in `src/platform/Input.cpp`, through
libultraship's per-port ignore lists and keyboard and mouse mappings). The choice
is the string CVar `gPort1DeviceKey` to `gPort4DeviceKey`
(`Platform::Input::DeviceKey`: empty, `none`, `kbm` or `pad:<n>:<name>`); the
earlier `gPortNDevice` integers are not read, so every port starts on Default
bindings.

On a fresh install port 1 picks a device itself, so a new player gets Modern
controls without opening the menu: Keyboard & Mouse when both are attached,
otherwise the first gamepad (the GamePad on the Wii U); with nothing attached it
tries again every frame until something is. It happens once (`DecideAutoPick`,
`AutoPickDevice` in `src/platform/BindingsApply.cpp`) and never overrides the
player: a saved device on any port, or mappings of their own (anything the
bindings migration carried over or the editor saved), end it without a change,
and so does choosing any device in the dropdown, Default bindings and None
included. The CVar `gDeviceAutoPicked` records that it is over. The other
ports are not touched.

A gamepad is saved by name and by its place among attached gamepads with that
name, because libultraship's ids don't survive a restart. Identical gamepads
are listed as "Name" and "Name (2)" and keep that order while attached. A chosen
device that is unplugged stays in the list as "<name> (disconnected)" and stays
saved; the port reads no input. When another port reads the same device, a
warning under the dropdown names those ports; port 1 on Default bindings counts
as reading every device.

The logic (keys, routing, dropdown entries, warning, reasons, notice) is pure and
lives in `src/platform/PortAssignment.*`, tested in
`tests/platform/PortAssignmentTest.cpp`.

The Set Defaults button at the bottom of the tab, after a confirmation, clears
every port's mappings and restores libultraship's default keyboard, mouse and
gamepad bindings on port 1 (`Platform::Input::SetDefaultBindings()`) and resets
the binding store. A Button widget with `Confirm(message)` asks before running its
callback; the port tabs' Reset to default uses the same widget.

## Menu CVars

- `gMenuOpacity`: panel background opacity (0.1 to 1.0, default 0.85).
- `gControlNav`: libultraship's gamepad navigation switch.

The open/closed state is deliberately not persisted.

## Settings tab

`src/menu/tabs/SettingsTab.cpp` (General, plus the About column),
`SettingsAudio.cpp` and `SettingsGraphics.cpp`. Everything the tab needs from
the window, audio output or host OS goes through the `System` interface in
`src/menu/System.h`; `SystemImpl.cpp` backs it with libultraship's `Context`, and
the unit tests use the default (no window, no audio) implementation. Settings
the current platform or backend can't honour stay visible but disabled, with the
reason as a tooltip. The pure helpers (combo choices, `file://` URLs, the version
line) are in `SettingsModel.cpp`.

## Dev Tools tab

It edits a copy of one folder's save
(`src/game/save/SaveEditor.*`, no ImGui, unit-tested in
`tests/game/SaveEditorTest.cpp`); nothing reaches the save until Apply, which
writes through the save manager and then offers a restart (the Restart now
button is the menu's reset). Sidebars: the folder and its pending changes,
the level-reward cheats, per-level completion and best time on each
difficulty, and the 007 mode flag. The expanded intro cast and the Magnum,
Laser and Golden Gun come from level completions, so they are set there.
Edits keep to states the game can produce: a completion implies the easier
difficulties' completions, and times stay in the stored 10-bit range.
The tab is registered by `Install()` rather than `RegisterTabs()` because it
needs the running game's save manager.

The Stage cheats sidebar switches cheats in the stage being played, the way
their button codes or the cheat menu would (`src/game/DevCheats.*`). The menu
only asks; the game loop applies the asks between frames through
`Stage::SetCheatFromDevTools` and publishes what the first player then has on.
A cheat that stays on is a checkbox. One that acts once when switched on (the
weapons and the health and ammo top-ups) is a button, since the game has no way
to switch it off. Cheats flagged for the other kind of game are
disabled with the reason.
