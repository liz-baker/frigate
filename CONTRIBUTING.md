# Contributing

Thanks for wanting to help. Frigate is a 1:1, multi-platform port of
*GoldenEye 007* (see the [README](README.md) and [docs/design.md](docs/design.md)
for what that means), and most of the rules below follow from that goal.
By taking part you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).

## Before you start

- Look through the [open issues](https://github.com/liz-baker/frigate/issues).
  Issues labeled `good first issue` or `help wanted` are the best starting
  points.
- For anything larger than a small fix, comment on the issue (or open one)
  before writing code, so we can agree on the approach and avoid duplicate work.
- Questions are welcome as issues.
- **Legal:** do not contribute ROM data, extracted game assets, or text or
  comments copied from the GoldenEye decompilation (`external/007`). It is
  unlicensed upstream; only bare facts such as opcode values or symbol names
  are safe to use, in your own wording. Code adapted from another project
  (`goldeneye-pc-port`, `perfect_dark`, or anything else) must be attributed
  where it is adapted and listed in
  [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md).

## Setting up

Follow [docs/building.md](docs/building.md). In short:

```sh
git clone --recurse-submodules https://github.com/liz-baker/frigate
cd frigate
just build
just test
```

In a new git worktree, run `just worktree-init` first. It checks out
`external/libultraship-wiiu` at the pinned commit using the main checkout's
objects, so it needs no network. Don't symlink submodules between worktrees.

## Workflow

- Never push to `main`. Every change goes through a pull request from a branch
  on your fork or this repository.
- Keep a PR to one change. A refactor and a feature are two PRs.
- Keep the description short: what changed, why, and how you checked it.
  Mention the issue it closes (`Closes #123`).
- If you find follow-up work you won't do in this PR, file it as its own issue
  instead of leaving a note in a comment or a TODO without a number.
- CI must pass: build on Linux and macOS, unit tests, and `clang-format`.

## Code

### Reuse before reimplementing

Platform-layer code (windowing, rendering, audio output, input, config,
crash handling) comes from libultraship. If you are about to write something
the library should provide, check whether it already does; if it should but
doesn't, the fix belongs in `libultraship-wiiu`, not a local copy here.

### Porting decompiled code

Port game logic **functionally 1:1**: every real side effect of the original,
including timing constants, lighting and camera setup and sound cues, not only
what is needed to get something on screen. Something that loads and runs can
still be wrong in a way that is expensive to find later.

If a piece of behavior belongs in a later change (input, audio, a scene not yet
in scope), leave a `// TODO(<issue URL>): ...` that cites the issue. A flagged
deferral is a decision; an unflagged gap is a bug.

Reuse the conventions the port already has, such as how a prop's placed matrix
and world position are combined, instead of re-deriving them.

### Determinism

Every attract demo replays a recording and checks the game's random-byte
stream against it, so a change that shifts the simulation shows up as a
mismatch. Run the demos before sending a change to `src/level`, `src/game`
or rendering code that feeds a stage's culling. All 14 should match except
demo 1, which has a known divergence. See [docs/testing.md](docs/testing.md#attract-demo-determinism).

### Style

- C++20, formatted with `clang-format` (version 14, the repository's
  `.clang-format`: 4-space indent, 120 columns).
- Match the code around you: naming, idiom and comment density.
- A comment earns its place by explaining a non-obvious *why*: a constraint, a
  workaround, an invariant. Don't restate the code or narrate how it got
  there; git history covers that.
- Binary and file-format layouts go in [`docs/formats/`](docs/formats/), and a
  comment points at the doc.
- In prose, call `external/007` "the GoldenEye decomp" and
  `external/libultraship-wiiu` "libultraship".

## Tests

New behavior comes with a test next to the code it covers (`tests/` mirrors
`src/`). [docs/testing.md](docs/testing.md) lists every check and when to run
it. In the PR, say what you ran and on what: software rendering can miss
bugs a real GPU or a console shows, so for rendering changes say where you
looked at it.

## Documentation

Update the docs in the same PR as the code they describe. A new doc gets a line
in [docs/README.md](docs/README.md).

## Reporting bugs

Open an issue with the platform and build type, what you expected, what
happened, and the steps to reproduce. Screenshots help for rendering bugs, and
the log if the game crashed. Do not attach ROMs or extracted game data.

## Licensing

Your contributions are licensed under the repository's [MIT license](LICENSE).
