# Frigate

A **multi-platform** port of *GoldenEye 007*, built on
[libultraship](https://github.com/kenix3/libultraship) — the
reimplementation of Nintendo 64's `libultra` SDK that powers
[Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) and
[2 Ship 2 Harkinian](https://github.com/HarbourMasters/2ship2harkinian).
Vendors [`libultraship-wiiu`](https://github.com/liz-baker/libultraship-wiiu)
specifically — a fork that tracks upstream libultraship (so it still carries
every platform upstream already supports) while adding Wii U (CafeOS) as an
additional target. Wii U is one target this project cares about, not the
premise of the project.

## What this is

GoldenEye 007 and Perfect Dark (Rare's "Indy" engine family) don't use
Nintendo's stock F3D/F3DEX2 graphics microcode the way *Ocarina of Time*
and *Majora's Mask* do — Rare wrote their own. That's the core problem this
port exists to solve, on whichever platforms libultraship targets: get
GoldenEye's decompiled source running against libultraship's existing
platform layer (rendering backend, audio, input) per-platform, rather than
a from-scratch platform layer written once per target the way existing
GoldenEye/Perfect Dark PC ports do today.

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
ucode reimplementation) — see [Roadmap](#roadmap) for how that's split.

## Structure

```
external/007                  GoldenEye 007 decompilation (submodule)
external/libultraship-wiiu    libultraship (submodule; the Wii U fork,
                               which still carries every platform upstream
                               libultraship already supports)
```

Both are vendored as git submodules rather than forked-into, so this repo's
own history and license stay cleanly separate from the decompilation's
(see [Legal](#legal)) and from upstream's. Clone with:

```
git clone --recurse-submodules https://github.com/liz-baker/frigate
```

## Status

Early planning. No port code has been written yet — this repo currently
holds the roadmap (see [Issues](https://github.com/liz-baker/frigate/issues)).
The graphics/audio microcode work below (#2/#3) is where actual engine work
starts, and isn't gated on any single platform's readiness.

## Roadmap

Tracked as issues in this repo. Rough order of operations:

1. Land Rare "Indy" engine graphics-opcode support in `libultraship-wiiu`
   (shared-library work, not specific to this port).
2. Port the audio microcode reimplementation (bespoke to this port — no
   shared-library home for this one; see the issue for why).
3. Wire this port's build against libultraship's existing platform shims
   instead of hand-rolling new ones, per target platform.
4. Build GoldenEye's asset pipeline on libultraship's O2R archive system
   (an offline packer adapting the decomp's own extraction knowledge,
   plus runtime resource types) rather than baking assets into the
   binary as C source — needed for mod/overlay support and texture packs.
5. On-hardware/per-platform rendering and audio validation.
6. Incremental integration: boot chain → input → audio → graphics → full
   campaign.

Whether the Wii U specifically ends up a viable target (its CPU is
considerably weaker than other platforms these microcode decoders already
run on) is a separate, platform-specific question tracked outside this
repo — not a gate on any of the above, since the work above is needed
regardless of how that resolves.

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
