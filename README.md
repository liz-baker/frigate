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
Wii U is one of the targets, not the premise.

Frigate vendors [`libultraship-wiiu`](https://github.com/liz-baker/libultraship-wiiu),
a fork that adds Wii U support, GoldenEye/Perfect Dark graphics-opcode support
and a few fixes on top of everything upstream supports. The longer story is in
[docs/design.md](docs/design.md).

## Status

Work in progress; there are no release builds yet (see
[#481](https://github.com/liz-baker/frigate/issues/481)). Today the game boots
through the intro and menus, plays the attract demos and runs stages in single
player and 2 to 4 player split-screen multiplayer. 13 of the 14 attract demos
replay in step with their recordings. Wii U runs but is still being optimized.
Progress and known problems are tracked in
[issues](https://github.com/liz-baker/frigate/issues) and on the
[project board](https://github.com/users/liz-baker/projects/1).

## Quick start

You need your own US GoldenEye 007 ROM (see [Legal](#legal)). Frigate extracts
its game archives from it on first launch.

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
