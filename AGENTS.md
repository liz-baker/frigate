# AGENTS.md

A multi-platform port of GoldenEye 007, built on libultraship. Vendors
[`libultraship-wiiu`](https://github.com/liz-baker/libultraship-wiiu) (a fork
adding Wii U support on top of everything upstream supports); Wii U is one
target, not the premise. `README.md` has the what/why and status;
`docs/README.md` indexes the rest.

## Structure

- `external/007`: the GoldenEye decomp, a git submodule. Unlicensed upstream:
  see Licensing before copying anything from it.
- `external/libultraship-wiiu`: the libultraship fork, a git submodule.
- `src/dev/`: developer-only launch arguments and the modes behind them
  (`--demo`, `--level`, `--multiplayer`, `--play-sound`, ...), left out of
  Release builds, which accept only `--archive-dir`, `--rom` and `--no-extract`. New test or
  verification flags go here, never in `main.cpp` directly.

Update a submodule with `git submodule update --remote external/<name>`, then
commit the new pinned commit.

## Workflow

- **Never push or merge directly to `main`.** Every change is a pull request,
  opened ready for review.
- Don't mention private tooling or projects in anything public (issues, PRs,
  commits, comments, docs). If work depends on something learned there,
  describe the finding itself.
- Use the `just` recipes (`just build`, `just test`, `just attract N`, ...)
  rather than raw build or run commands.
- Releases are cut by pushing a `vX.Y.Z` tag (`docs/releasing.md`).

## Worktrees

Run `just worktree-init` first in every new worktree (agents included). It
checks out `external/libultraship-wiiu` at the pinned commit, borrowing
objects from the main checkout, so it needs no network; `just worktree-init
all` also checks out `external/007`, which the recipes that read the decomp
(`gen-tables`, `check-tables`, `ref-archives`, ...) need. No other recipe
touches submodules. A missing libultraship is a configure error, so `just
build` and `just test` fail loudly. They reuse the main checkout's
`build/_deps` sources when present.

Don't symlink the submodules to the main checkout: git reports a typechange
and every worktree is pinned to the main checkout's version.

The recipes configure with `FRIGATE_REQUIRE_GAME=ON`, which makes a missing
libultraship a configure error instead of silently skipping the game targets.
Plain `cmake` leaves it off so CI's submodule-free test job keeps working.

## Issues and the project board

- Every issue is a deliverable chunk of work. When an issue's remaining scope
  has shrunk to something another issue owns, close it, fold the remainder
  into that issue (reference the closed one), and explain the fold in the
  closing comment. Don't leave two issues pointing at each other.
- Narrate scoping and status as issue comments while work is active. Follow-up
  work that won't happen in the current PR gets its own issue immediately; a
  comment is not a tracking mechanism.
- Keep umbrella issues' sub-issue lists in sync as sub-issues open, close or
  fold. Use native GitHub sub-issues, not markdown checklists.
- The [project board](https://github.com/users/liz-baker/projects/1) tracks
  the initial-release milestone. `Status` syncs from open/closed state;
  `Priority` (`P0 - Next` / `P1 - Soon` / `P2 - Later` / `Parked`) is manual:
  keep it current.

## Design principle: reuse before reimplementing

The two existing GoldenEye/Perfect Dark PC ports each hand-rolled a platform
layer (OS shim, config, crash handling, input) that libultraship already
provides generically. Before writing platform-layer code here, check whether
libultraship solves it; if it looks like re-deriving something the library
provides, use the library's version.

The one deliberate exception is GoldenEye's custom RSP microcode. Graphics
opcode support landed upstream (liz-baker/libultraship-wiiu#28); the audio
microcode reimplementation stays bespoke to this port.

## Porting decomp game code

Port a decomp function (menu constructors, interface routines, any game-logic
routine) functionally 1:1: every real side effect (timing constants, lighting
and camera setup, sound cues, ...), not just what's needed to get something
rendering. Something that loads and runs can still be wrong in a way that is
expensive to diagnose, because the symptom (invisible geometry) doesn't point
at the cause (a missing light setup).

If a behavior belongs in a later issue (input, audio, a scene not yet in
scope), say so with a `// TODO(<issue URL>): ...` comment. An unflagged gap is
a bug.

Run the `porting-fidelity` skill (`.claude/skills/porting-fidelity/SKILL.md`)
against the diff before treating a porting PR as done.

Reuse existing code paths for conventions this port has already solved (matrix
and coordinate conventions, state splits, resource lookups). For example a
prop's placed matrix and its world position are separate fields, mirroring the
decomp's `obj->mtx`/`obj->runtime_pos`, and must be recombined the way the
existing collision code does it.

## Attract demo determinism

Every attract demo replays a recording and checks the game's random-byte
stream against it, so any change that shifts the simulation (culling, an extra
random draw, a camera or state difference between demo and live play) shows up
as a mismatch. Run `just attract N --uncapped --no-abort-on-mismatch` and read
the "random byte matched X of Y packets" line. All 14 demos match except demo
1, which diverges at about packet 68 (tick 264). Treat any other mismatch as a
regression: bisect against `origin/main` before calling it pre-existing. Run
all 14 before merging anything touching `src/level`, `src/game` or rendering
code that feeds the stage's culling.

## Comment style

- A comment earns its place by explaining a non-obvious *why* (a constraint, a
  workaround, an invariant), not by restating the code or narrating
  background.
- Binary and file-format layouts belong in `docs/formats/`; comments point at
  the doc.
- Don't narrate a file's history; describe the code as it is. Git covers the
  rest.
- Gaps that need work are `// TODO(<issue URL>)`, filed first if untracked.
  Don't write "isn't confirmed yet" or similar hand-waving.
- Don't cite this file as a justification ("per AGENTS.md"); justify a
  decision on its own terms.
- In prose, call `external/007` "the GoldenEye decomp" and
  `external/libultraship-wiiu` "libultraship". Literal paths and commands stay
  literal, and a TODO citing a `libultraship-wiiu` issue names the fork.
- Don't annotate facts taken from the decomp with hedges like "(bare fact,
  fresh wording)"; the Licensing section is the one place that policy lives.

## Licensing and attribution

The policy lives in `CONTRIBUTING.md` (Legal) and `THIRD_PARTY_LICENSES.md`.
In short: the GoldenEye decomp is unlicensed, so never copy its prose or
comments, only bare facts in fresh wording; attribute adapted
`goldeneye-pc-port`/`perfect_dark` code at the point of adaptation and in
`THIRD_PARTY_LICENSES.md`.
