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

The Appearance sidebar in `src/menu/tabs/MenuTab.cpp` and the example tab in
`tests/menu/MenuModelTest.cpp` show the widget types in use.

## Menu CVars

- `gMenuOpacity`: panel background opacity (0.1 to 1.0, default 0.85).
- `gControlNav`: libultraship's gamepad navigation switch.
- `gDevTools`: shows the Dev Tools tab (Menu > Appearance > Developer tools). Off by default.

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

Hidden until `gDevTools` is on. It edits a copy of one folder's save
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
