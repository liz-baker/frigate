# CLAUDE.md

A multi-platform port of GoldenEye 007, built on libultraship. Vendors
[`libultraship-wiiu`](https://github.com/liz-baker/libultraship-wiiu)
specifically (a fork adding Wii U support on top of everything upstream
already supports) — Wii U is one target platform, not the premise of this
project. See `README.md` for the what/why and current status.

## Structure

- `external/007` — the GoldenEye 007 decompilation, vendored as a git
  submodule. Unlicensed upstream (see `README.md#legal`) — never assume
  code or comments here can be copied elsewhere without checking that
  section first.
- `external/libultraship-wiiu` — the Wii U libultraship fork, vendored as a
  git submodule.

Update either with the normal submodule flow (`git submodule update
--remote external/<name>`, then commit the new pinned commit).

## Workflow

**Never push or merge directly to `main`.** Every change goes through a
pull request for review, no exceptions — same convention as
`libultraship-wiiu` and the sibling harness project used for this port's
Wii U testing specifically.

**Do not reference the private test harness project in anything public**
(issues, PRs, commit messages, code comments) — it's a personal tool, not
part of this port's public surface. If work here depends on something
learned or validated there, describe the finding itself, not where it came
from.

## Design principle: reuse before reimplementing

This port exists specifically *because* the two existing GoldenEye/Perfect
Dark PC ports each hand-rolled a full platform layer (OS shim, config,
crash handling, input) that `libultraship-wiiu` already provides generically
and has hardened across multiple platforms. Before writing any new
platform-layer code here, check whether `libultraship-wiiu` already solves
it — if it looks like it's re-deriving something the library should already
provide, that's a signal to use the library's version instead, not to
extend this repo's own copy. This mirrors the same principle
`lus-wiiu-harness` (the sibling Wii U test project) already operates under.

The one deliberate exception: GoldenEye's custom RSP microcode (graphics
*and* audio) isn't something `libultraship-wiiu` can provide out of the box.
Graphics-opcode support belongs upstream in `libultraship-wiiu` itself (it
already has the extension point for it); the audio microcode
reimplementation is bespoke to this port with no shared-library home — see
the roadmap issues for the reasoning behind that split.

## Licensing and attribution

Code adapted from `goldeneye-pc-port` or `perfect_dark` (or, transitively,
the MIT-licensed `fast3d`/`gfx_pc.cpp` engine underneath both) must be
attributed to its actual origin at the point it's adapted — a source
comment plus an entry in a `THIRD_PARTY_LICENSES`/`NOTICE`-style file, not
folded silently into this repo's own MIT license. Never copy prose or
comments from `external/007` itself into anything with a different
license — it's unlicensed upstream; only treat bare facts (e.g. opcode
values) from it as safe to use, and only in fresh wording.
