# Frigate

A **multi-platform** port of *GoldenEye 007*, built on
[libultraship](https://github.com/kenix3/libultraship) — the
reimplementation of Nintendo 64's `libultra` SDK that powers
[Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) and
[2 Ship 2 Harkinian](https://github.com/HarbourMasters/2ship2harkinian).
Vendors [`libultraship-wiiu`](https://github.com/liz-baker/libultraship-wiiu)
specifically — a fork that adds some changes specific for Goldeneye & Perfect
Dark, and fixes some bugs that were not hit by other ports

## What this is

GoldenEye 007 and Perfect Dark (Rare's "Indy" engine family) don't use
Nintendo's stock F3D/F3DEX2 graphics microcode the way *Ocarina of Time*
and *Majora's Mask* do — Rare wrote their own. That's the core problem this
port exists to solve, on whichever platforms libultraship targets: get
GoldenEye's decompiled source running against libultraship's existing
platform layer (rendering backend, audio, input) per-platform, rather than
a from-scratch platform layer written once per target the way existing
GoldenEye/Perfect Dark PC ports do today.

## Why start a new port
I was already in the process of re-porting `libultraship` to Wii U, because
the existing wiiu fork was behind and could not be cleanly updated. 
Originally, my goal was to then go back and re-fork Ship of Harkinian since
I just wanted to play OoT randomizers on my WiiU, but during that work the
Goldeneye decomp dropped. Since `libultraship` is already battle-hardened
and provides most of what was needed, and looking at the other port projects
that were not going to be cross platform (or run on my Wii U!). I decided
to start my own.

## Why build on libultraship instead of a from-scratch PC-style port

Existing GoldenEye/Perfect Dark PC ports
([`goldeneye-pc-port`](https://github.com/jkdansereau/goldeneye-pc-port),
[`perfect_dark`](https://github.com/perfect-dark-pc-port/perfect_dark)) are
real, working, hardware-validated software — but each one hand-rolls its own
full platform layer from scratch: a ~1,800-line libultra OS shim, a from-
scratch config/options-menu system, a from-scratch crash handler, SDL-based
input. Some of that is duplicated *between* those two ports independently,
each solving the same problem a second time with no shared library at all.

libultraship already has hardened, cross-platform implementations of all
of that: `Context`/`ControlDeck`/`ThreadPool` for the OS layer,
`Config`/`ConsoleVariable` plus an established ImGui options-menu
convention, an existing crash handler, and rendering/audio/input backends
per platform — including, via the `libultraship-wiiu` fork specifically,
`GfxRenderingAPIGX2`/`GfxWindowBackendWiiU` for a working native Wii U
renderer we'd otherwise have to write from nothing on that one target.

The one thing libultraship's `Fast::Interpreter` *doesn't* already handle is
GoldenEye's custom microcode — but it already has the exact extension point
built for this (`ucode_handlers[]`, the same mechanism that already holds
`f3d`/`f3dex`/`f3dex2`/`s2dex` side by side). Filling that in once, in the
shared library, benefits this port *and* any future Perfect Dark or similar
port — instead of duplicating a third from-scratch platform layer the way
the two existing PC ports did to each other.

In short: reuse what's already solved generically, and only build what's
genuinely specific to Rare's engine (the microcode opcode work, the audio
ucode reimplementation) — see [Roadmap](docs/roadmap.md) for how that's split.

## Structure

```
external/007                  GoldenEye 007 decompilation (submodule)
external/libultraship-wiiu    libultraship (submodule; the Wii U fork)
```

Both are vendored as git submodules rather than forked-into, so this repo's
own history and license stay cleanly separate from the decompilation's
(see [Legal](#legal)) and from upstream's. Clone with:

```
git clone --recurse-submodules https://github.com/liz-baker/frigate
```

## Legal

**You must supply your own legally-owned GoldenEye 007 ROM.** This
repository contains no Nintendo or Rare code, assets, or ROM data.

This repo's own code (build integration, per-platform wiring) is
MIT-licensed — see [`LICENSE`](./LICENSE). `external/007` (the
decompilation) is inherited from
[`n64decomp/007`](https://github.com/n64decomp/007) and, as with essentially
all N64 decompilation projects, is published without an explicit license —
that's a property of the upstream decompilation, not something this repo's
license changes. Any code adapted from `goldeneye-pc-port`/`perfect_dark`
(both MIT, with the actual Fast3D/`gfx_pc.cpp` engine underneath both
separately MIT-licensed to Emill & MaikelChan) will be attributed to its
real origin at the point it's adapted, not folded silently into this repo's
own license.

GoldenEye 007 is a trademark of its respective rights holders. This is a
non-commercial, fan-made porting effort, not affiliated with or endorsed by
Nintendo, Rare, MGM, EON Productions, Danjaq, or any other rights holder.
