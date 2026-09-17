# Frigate

A Wii U (CafeOS) port of *GoldenEye 007*, built on
[`libultraship-wiiu`](https://github.com/liz-baker/libultraship-wiiu) —
the Wii U fork of [libultraship](https://github.com/kenix3/libultraship),
the reimplementation of Nintendo 64's `libultra` SDK that powers
[Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) and
[2 Ship 2 Harkinian](https://github.com/HarbourMasters/2ship2harkinian).

## What this is

GoldenEye 007 and Perfect Dark (Rare's "Indy" engine family) don't use
Nintendo's stock F3D/F3DEX2 graphics microcode the way *Ocarina of Time*
and *Majora's Mask* do — Rare wrote their own. That's the core problem this
port exists to solve on Wii U: get GoldenEye's decompiled source running
against `libultraship-wiiu`'s Wii U-specific work (the GX2 renderer, native
AX audio, VPAD/KPAD input) rather than a from-scratch platform layer.

## Why build on libultraship instead of a from-scratch PC-style port

Existing GoldenEye/Perfect Dark PC ports
([`goldeneye-pc-port`](https://github.com/jkdansereau/goldeneye-pc-port),
[`perfect_dark`](https://github.com/perfect-dark-pc-port/perfect_dark)) are
real, working, hardware-validated software — but each one hand-rolls its own
full platform layer from scratch: a ~1,800-line libultra OS shim, a from-
scratch config/options-menu system, a from-scratch crash handler, SDL-based
input. Some of that is duplicated *between* those two ports independently,
each solving the same problem a second time with no shared library at all.

`libultraship-wiiu` already has hardened, cross-platform (and now
Wii-U-specific) implementations of all of that: `Context`/`ControlDeck`/
`ThreadPool` for the OS layer, `Config`/`ConsoleVariable` plus an established
ImGui options-menu convention, an existing crash handler, and — critically —
`GfxRenderingAPIGX2`/`GfxWindowBackendWiiU` for a working native Wii U
renderer we'd otherwise have to write from nothing.

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
external/libultraship-wiiu    libultraship, Wii U fork (submodule)
```

Both are vendored as git submodules rather than forked-into, so this repo's
own history and license stay cleanly separate from the decompilation's
(see [Legal](#legal)) and from upstream's. Clone with:

```
git clone --recurse-submodules https://github.com/liz-baker/frigate
```

## Status

Early planning. No port code has been written yet — this repo currently
holds the roadmap (see [Issues](https://github.com/liz-baker/frigate/issues))
and will start seeing real work once Wii U `libultraship-wiiu` port testing
(in a separate, private harness) is further along.

## Roadmap

Tracked as issues in this repo. Rough order of operations:

1. Validate the CPU cost of software microcode decoding on real Wii U
   (Espresso) hardware before investing further — the single biggest open
   risk.
2. Land Rare "Indy" engine graphics-opcode support in `libultraship-wiiu`
   (shared-library work, not specific to this port).
3. Port the audio microcode reimplementation (bespoke to this port — no
   shared-library home for this one; see the issue for why).
4. Wire this port's build against `libultraship-wiiu`'s existing platform
   shims instead of hand-rolling new ones.
5. Wire up the decomposition's own asset-extraction/build pipeline
   unmodified — no attempt to route GoldenEye's assets through
   libultraship's OTR/O2R archive system.
6. On-hardware rendering/audio validation.
7. Incremental integration: boot chain → input → audio → graphics → full
   campaign.

## Legal

**You must supply your own legally-owned GoldenEye 007 ROM.** This
repository contains no Nintendo or Rare code, assets, or ROM data.

This repo's own code (build integration, Wii U-specific platform wiring) is
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
