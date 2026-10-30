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

## Worktrees

Run `just worktree-init` first in every new worktree (agents included)
before building. It checks out `external/libultraship-wiiu` at the pinned
commit, borrowing objects from the main checkout via `--reference` so it
needs no network when the commit exists there; `just worktree-init all`
also checks out `external/007`, which the recipes that read the decomp
(`gen-tables`, `check-tables`, `ref-archives`, ...) need. No recipe
touches the submodules itself; `just worktree-init` is the one explicit
step. A missing libultraship is a configure error, so `just build` and
`just test` fail loudly instead. They reuse the main checkout's `build/_deps` sources for imgui,
monocypher, prism and threadpool when they exist.

Don't symlink the submodules to the main checkout: git reports that as a
typechange, and it pins every worktree to the main checkout's version.

The recipes configure with `FRIGATE_REQUIRE_GAME=ON`, which makes a
missing libultraship a configure error instead of a silently skipped set
of game targets. Plain `cmake` leaves it off so the submodule-free
configuration in CI's test-validation job keeps working.

## Issue, scope, and project-board management

Every issue should be a fully deliverable chunk of work on its own. When a
scoping pass or in-progress work shows that an issue's real remaining scope
has shrunk to something another issue already owns (or has migrated there
entirely), close it and fold the remainder into the issue that actually
owns it — reference the closing issue in the survivor's body, and explain
the fold in the closing comment. Don't leave a tracking issue open just to
babysit a single line item; don't leave two issues pointing at each other
for the same remaining piece of work either.

Record scoping and status updates as issue comments while work is active
— that's the right place to narrate what's done and what's next. But the
moment a piece of follow-up work is identified that won't happen in the
current PR, file it as its own issue immediately, not just a comment note.
A comment is not a tracking mechanism; if the work survives past the PR
that found it, it needs its own issue number.

Parent/umbrella issues that list sub-issues in a checklist (e.g. #5) must
be kept in sync with reality as those sub-issues open, close, or get
folded elsewhere — check off what's done, remove what's closed, add what's
newly filed. A stale checklist is worse than no checklist, since it makes
finished work look outstanding and hides what's actually left.

The project board at https://github.com/users/liz-baker/projects/1 tracks
the initial-release milestone specifically, not every issue in the repo.
Its `Status` field syncs automatically from an issue's open/closed state;
its `Priority` field (`P0 - Next` / `P1 - Soon` / `P2 - Later` / `Parked`)
is manual and needs to be set and kept current by hand when priorities
shift.

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
Graphics-opcode support has already landed upstream in `libultraship-wiiu`
(liz-baker/libultraship-wiiu#28); the audio microcode reimplementation
remains bespoke to this port, with no shared-library home — see the
roadmap issues for the reasoning behind that split.

## Porting decomp game code

When porting a decomp function (an `init_menuXX_*`/`interface_menuXX_*`/
`constructor_menuXX_*`, or any other game-logic routine) into this repo,
port it functionally 1:1 — every real side effect the original has
(timing constants, lighting/light setup, camera/viewport setup, sound
cues, etc.), not just whatever's obviously necessary to get something
rendering. Silently dropping a real piece of the original's behavior is
not an acceptable shortcut: something that looks like it works (loads,
runs, no errors) can still be functionally wrong in a way that's
expensive to diagnose later, because the symptom (e.g. invisible
geometry) doesn't point back at the actual cause (a missing light setup)
at all.

If a specific piece of behavior genuinely belongs in a later issue
(controller input, audio, a scene not yet in scope), that's fine — but
say so explicitly, with a comment citing the issue, not by simply leaving
it out. "I didn't port X yet because Y is tracked in #N" is a decision;
an unflagged gap is a bug waiting to be found the hard way.

Before treating any porting PR as done, run the `porting-fidelity` skill
(`.claude/skills/porting-fidelity/SKILL.md`) against the diff to catch
drift of exactly this kind before it ships, rather than relying on it
being caught in review after the fact.

When new porting code needs something this port has already solved
elsewhere (a coordinate/matrix convention, a state split like keeping an
object's transform and its world position in separate fields, a resource
lookup pattern), reuse the existing code path instead of re-deriving the
same result inline. A prop's placed matrix and its world position are
two separate fields (mirroring the decomp's own `obj->mtx`/
`obj->runtime_pos` split) that must be recombined before use exactly the
way the existing collision code already does it — copying that pattern,
not reinventing it, is what keeps a second implementation from getting
the combination subtly wrong.

## Attract demo determinism

Every attract demo replays a recording and checks the game's random-byte
stream against it, so any change that shifts the simulation (a culling
change, an extra random draw, a camera or state difference between demo
and live play) shows up as a mismatch. All 14 demos match their recordings
(`just attract N --uncapped --no-abort-on-mismatch`, then read the
"random byte matched X of Y packets" line) except demo 1, which has a known
divergence at about packet 68 (tick 264). Treat any other mismatch as a
regression: bisect it against `origin/main` before calling it pre-existing.
Run all 14 before merging anything that touches `src/level`, `src/game`
or rendering code that feeds the stage's culling.

## Comment style

- No pointless prose. A comment earns its place by explaining a
  non-obvious *why* (a constraint, a workaround, a subtle invariant) —
  not by restating what the code already says, and not by narrating
  background at essay length.
- Binary/file-format descriptions (on-disk struct layouts, header byte
  tables, etc.) belong in `docs/formats/`, not inline. Code comments
  should point at the doc (e.g. "on-disk layout: docs/formats/foo.md"),
  not repeat it.
- Refer to `external/007` as "the GoldenEye decomp" in prose. Keep
  literal paths/commands (`external/007/scripts/...`, `git submodule
  update --init external/007`) as literal paths — this only applies to
  descriptive references to the project itself.
- Refer to `external/libultraship-wiiu`/`libultraship-wiiu` as
  "libultraship" in prose — that's the upstream library's actual name,
  and what the CMake target/namespace is called. The only exception is
  a `TODO` that cites a specific issue in the `libultraship-wiiu` GitHub
  project (e.g. `liz-baker/libultraship-wiiu#28`), where the fork's own
  name is the correct thing to cite. Literal paths (`.gitmodules`,
  `external/libultraship-wiiu/CMakeLists.txt`, CI steps naming the
  submodule) stay literal for the same reason as `external/007` above.
- Never write "per CLAUDE.md" (or any variant citing this file) as a
  justification inside a comment. If a decision needs justifying,
  justify it on its own terms; if this file's policy needs restating,
  restate the policy itself, don't cite the file.
- Don't write "isn't confirmed yet" (or similar hand-waving) for work
  that still needs doing. If there's a real gap, either it's already
  tracked — write `// TODO(<issue URL>): ...` — or it isn't yet, in
  which case file an issue first, then reference it the same way.
- Don't append "(bare fact, fresh wording)" or similar CYA hedges when
  citing a fact from `external/007` — the Licensing section below is the
  one place that policy needs stating; repeating it inline every time
  adds nothing.
- Don't narrate a file's own history ("originally lived in X, moved here
  when Y needed it"). A comment describes the code as it is now — git
  history is what covers how it got there.

## Licensing and attribution

Code adapted from `goldeneye-pc-port` or `perfect_dark` (or, transitively,
the MIT-licensed `fast3d`/`gfx_pc.cpp` engine underneath both) must be
attributed to its actual origin at the point it's adapted — a source
comment plus an entry in a `THIRD_PARTY_LICENSES`/`NOTICE`-style file, not
folded silently into this repo's own MIT license. Never copy prose or
comments from `external/007` itself into anything with a different
license — it's unlicensed upstream; only treat bare facts (e.g. opcode
values) from it as safe to use, and only in fresh wording. Symbolic
names (enum and record names such as `PROP_*` and weapon names) are
bare facts of this kind and are kept as written in the generated
tables committed under `src/extract` and `src/level`
(`tools/gen_tables.py`), not reduced to numeric ids.
