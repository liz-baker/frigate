# Design

Why Frigate is built the way it is. For how to build and run it, see
[building.md](building.md).

## Goal

A GoldenEye 007 that behaves like the N64 original and runs on more than one
platform. Two things follow from that and shape most decisions below:

- The game logic is a **functional 1:1 port** of the GoldenEye decompilation
  (`external/007`): same state machines, timing constants, randomness, camera
  and lighting setup, sound cues. Where the port is not the original, it is a
  deliberate, documented departure (see below), never an accident.
- Everything **platform-specific** comes from libultraship rather than being
  written per target.

## Where the project came from

I was already re-porting libultraship to Wii U, because the existing Wii U fork
was behind and could not be cleanly updated. The plan was then to re-fork Ship
of Harkinian so I could play *Ocarina of Time* randomizers on a Wii U. During
that work the GoldenEye decompilation was published. libultraship was already
hardened and provided most of what a port needs, and the existing GoldenEye
and Perfect Dark PC ports were not cross-platform (or going to run on my Wii
U), so I started this one.

## Why libultraship

The existing GoldenEye and Perfect Dark PC ports
([`goldeneye-pc-port`](https://github.com/jkdansereau/goldeneye-pc-port),
[`perfect_dark`](https://github.com/perfect-dark-pc-port/perfect_dark)) are
real, working software, but each hand-rolls a full platform layer: a libultra
OS shim of about 1,800 lines, a from-scratch options menu and config system, a
crash handler, SDL-based input. Parts of that are duplicated between the two
ports, each solving the same problem again with no shared library.

libultraship already provides hardened, cross-platform versions of all of it:

- `Context`, `ControlDeck` and `ThreadPool` for the OS layer.
- `Config` and `ConsoleVariable`, plus an ImGui menu convention.
- A crash handler.
- Rendering, audio and input backends per platform, including, through the
  `libultraship-wiiu` fork, a native Wii U renderer
  (`GfxRenderingAPIGX2`/`GfxWindowBackendWiiU`).

**Reuse before reimplementing** is the working rule: before writing
platform-layer code here, check whether libultraship already solves it. If new
code looks like it re-derives something the library should provide, use the
library's version or improve the library, rather than growing a second copy in
this repo.

## The custom microcode

Rare did not use Nintendo's stock F3D/F3DEX2 graphics microcode the way
*Ocarina of Time* and *Majora's Mask* do; GoldenEye and Perfect Dark run their
own graphics and audio microcode. That is the one thing libultraship cannot
provide out of the box, and the main reason this port exists as a separate
project.

- **Graphics.** libultraship's `Fast::Interpreter` has an extension point for
  microcode handlers (`ucode_handlers[]`, which already holds `f3d`, `f3dex`,
  `f3dex2` and `s2dex` side by side). The GoldenEye opcodes are filled in
  there, in the shared library, so any future Perfect Dark or similar port
  benefits too.
- **Audio.** The audio microcode is reimplemented in this repo (`src/audio`,
  see [audio.md](audio.md)). It has no shared-library home.

## Assets

The repository contains no game assets. Assets live in four O2R archives
(`goldeneye_textures.o2r`, `goldeneye_objects.o2r`, `goldeneye_geometry.o2r`,
`goldeneye_audio.o2r`), libultraship's archive format, which gives mod and
texture-pack overlays, hot reload and loose-folder development for free. The
game builds them itself from the player's ROM on first launch with the C++
extraction library in `src/extract` ([extract.md](extract.md)), so neither
Python nor the decompilation is needed to play. Binary layouts are documented
in [formats/](formats/).

Only the US ROM is supported so far.

## The decompilation as source of truth

`external/007` is a submodule. It is the reference for porting, the source of
the tables committed under `src/extract` and `src/level` (`tools/gen_tables.py`),
and the input to the reference packers and texture oracle that the extractor is
checked against. The build does not read it, so building and running the game
does not need it checked out. Porting a
function means reproducing every real side effect it has, and anything left for
later is marked with a `TODO` citing the issue that owns it. An unflagged gap
is treated as a bug.

## Verifying fidelity

- **Attract demos.** Each of the 14 attract demos replays the original's
  recording and checks the game's random-byte stream against it. Any change to
  the simulation, such as an extra random draw, a culling change or a camera
  difference, shows up as a mismatch. Run `just attract N --uncapped
  --no-abort-on-mismatch` and read the "random byte matched X of Y packets"
  line. 13 of 14 match; demo 1 diverges at about packet 68, as it does on the
  original hardware.
- **Unit tests** for the extractors, resource loaders, menu models and
  binding store (`just test`).
- **Headless harnesses** for multiplayer ([multiplayer-verification.md](multiplayer-verification.md))
  and extraction ([extract-verification.md](extract-verification.md)).

## Deliberate departures

Places where Frigate is intentionally not the N64 game, kept out of the way of
the simulation so the demos still match:

- **Modern controls** (mouse and keyboard, dual stick), an opt-in input mode.
  N64 mode reads the original mappings. See [modern-controls.md](modern-controls.md).
- **Menus and settings** drawn with ImGui over the game ([menu.md](menu.md)).
- **Display options** such as widescreen, since libultraship renders at any
  aspect ratio.

## Source layout

| Directory | Contents |
|---|---|
| `src/game` | Game-mode logic ported from the decompilation: characters, props, demos, multiplayer setup |
| `src/level` | Stage loading, visibility and rendering, split-screen layout, AI lists |
| `src/model` | Model and animation playback |
| `src/gfx`, `src/texture` | Fixed-point matrix and `gu` helpers, texture expansion |
| `src/audio` | Audio microcode, synthesis, sound and music players |
| `src/menu` | The ImGui configuration menu |
| `src/platform` | Input, bindings, boot and other glue to libultraship |
| `src/resource` | O2R resource types and their loaders |
| `src/extract` | The ROM extraction library (`frigate_extract`) |
| `tests/` | C++ unit tests and the multiplayer harness |
| `tools/` | Table generator, archive comparison, reference packers |

## Wii U

Wii U is a supported target, built through devkitPro with `just wiiu_build`.
Its CPU is much weaker than the other targets', so performance work there
(the Fast3D interpreter's per-triangle cost, the allocator) is ongoing. That
work is tracked in the issues and does not gate anything on the other
platforms.
