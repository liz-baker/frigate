# Frigate

A **1:1, multi-platform** port of *GoldenEye 007*, built on
[libultraship](https://github.com/kenix3/libultraship), the reimplementation
of Nintendo 64's `libultra` SDK that powers
[Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) and
[2 Ship 2 Harkinian](https://github.com/HarbourMasters/2ship2harkinian).

## Why this exists

I wanted to play the real GoldenEye, with the original's behavior intact, on
more than one kind of machine. The existing GoldenEye and Perfect Dark PC
ports are good, but each hand-rolls its own platform layer for a single
target. Frigate runs the GoldenEye decompilation's game logic as faithfully as
it can (an attract demo replays the original's recorded input and checks that
the game's random-number stream matches the recording) and leaves everything
platform-specific (windowing, rendering, audio output, input) to libultraship.

Frigate vendors [`libultraship-wiiu`](https://github.com/liz-baker/libultraship-wiiu),
a fork that adds Wii U support, GoldenEye/Perfect Dark graphics-opcode support
and a few fixes on top of everything upstream supports. The longer story is in
[docs/design.md](docs/design.md).

## Status

The game is essentially complete: the full single-player game, 2 to 4 player
split-screen multiplayer, the menus and the attract demos all work. The attract
demos match the original exactly: 13 of the 14 replay in step with their
recordings, and the other (demo 1) diverges from its recording at the same
point, and exits the same way, on the original hardware. Beyond the original
controls, there are modern controls for keyboard and mouse and for gamepads
([docs/modern-controls.md](docs/modern-controls.md)).

Known problems, including Wii U performance work, are tracked in
[issues](https://github.com/liz-baker/frigate/issues).

## Quick start

You need your own US GoldenEye 007 ROM (see [Legal](#legal)). Frigate extracts
its game archives from it on first launch.

Each [release](https://github.com/liz-baker/frigate/releases) has packages for
Windows (zip), macOS (universal zip), Linux (AppImage) and Wii U (zip to extract
on the SD card root). Put the ROM next to the executable, or on Wii U in
`/wiiu/apps/frigate/` (on macOS, in `~/Library/Application Support/frigate`), and start it. The macOS and Linux packages are unsigned;
[docs/releasing.md](docs/releasing.md) has the details.

To build from source instead:

```sh
git clone --recurse-submodules https://github.com/liz-baker/frigate
cd frigate
just build              # needs cmake, a C++20 compiler and the libraries in docs/building.md
cp /path/to/GoldenEye\ 007\ \(USA\).z64 .
just run
```

[docs/building.md](docs/building.md) has the dependencies per platform, the
Wii U build, running without `just`, and troubleshooting.

## Documentation

[docs/README.md](docs/README.md) indexes everything: how to build, the design
and the reasoning behind it, subsystem notes, binary format specifications and
the testing guides.

## Contributing

Pull requests are welcome; read [CONTRIBUTING.md](CONTRIBUTING.md) first, and
note the [Code of Conduct](CODE_OF_CONDUCT.md).

## Legal

**You must supply your own legally-owned GoldenEye 007 ROM.** This
repository contains no Nintendo or Rare code, assets, or ROM data.

This repo's own code (build integration, per-platform wiring) is
MIT-licensed; see [`LICENSE`](./LICENSE). `external/007` (the
decompilation) is inherited from
[`n64decomp/007`](https://github.com/n64decomp/007) and, as with essentially
all N64 decompilation projects, is published without an explicit license.
That is a property of the upstream decompilation, not something this repo's
license changes. Code adapted from `goldeneye-pc-port` or `perfect_dark`
(both MIT, with the Fast3D `gfx_pc.cpp` engine underneath both separately
MIT-licensed to Emill & MaikelChan) is attributed to its real origin where it
is adapted; see [`THIRD_PARTY_LICENSES.md`](./THIRD_PARTY_LICENSES.md).

GoldenEye 007 is a trademark of its respective rights holders. This is a
non-commercial, fan-made porting effort, not affiliated with or endorsed by
Nintendo, Rare, MGM, EON Productions, Danjaq, or any other rights holder.
