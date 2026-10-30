# Testing

What checks exist, what each one catches, and when to run it. Setup is in
[building.md](building.md).

| Check | Command | Catches | Needs |
|---|---|---|---|
| Unit tests | `just test` | Regressions in the extractors, resource loaders, menu and binding models, game logic, and the Python tools | Nothing; ROM-dependent tests skip themselves |
| Formatting | `clang-format` (see below) | Style drift; CI rejects it | clang-format 14 |
| Generated tables | `just check-tables` | Committed tables out of date with the decompilation | `external/007` (`just worktree-init all`) |
| Attract demos | `just attract N --uncapped --no-abort-on-mismatch` | Any change to the simulation | Packed archives |
| Multiplayer harness | `just mp_test` | Split-screen, per-player state, menus-to-stage hand-off | Packed archives |
| Extraction parity | `just verify-extract <rom>` | The C++ extractor differing from the reference packers | A ROM |

## Unit tests

`just test` builds everything and runs `ctest`. C++ tests live under `tests/`,
mirroring `src/`; the Python tools have their own `unittest` suites next to
them. A new behavior should land with a test beside the code it covers.

## Attract demo determinism

Each of the 14 attract demos replays the original's recorded input and checks
the game's random-byte stream against the recording. The simulation has to
stay identical to the original for them to match, so this is the main guard
against drift from a 1:1 port.

```sh
just attract 6 --uncapped --no-abort-on-mismatch
```

Read the "random byte matched X of Y packets" line. All 14 demos match their
recordings except demo 1, which diverges at about packet 68 (tick 264); that
is known. Any other mismatch is a regression: bisect it against `origin/main`
before calling it pre-existing. Run all 14 before merging anything that
touches `src/level`, `src/game`, or rendering code that feeds a stage's
culling.

## Multiplayer

The attract demos carry no multiplayer game, so multiplayer has its own
headless harness and a manual checklist:
[multiplayer-verification.md](multiplayer-verification.md).

## Extraction

The C++ extractor must produce the same archives, byte for byte, as the Python
reference packers in `tools/o2r_packer`:
[extract-verification.md](extract-verification.md).

## Formatting

CI runs `clang-format` over the sources with the repository's `.clang-format`
(4-space indent, 120 columns). Format before you push; CI pins clang-format 14,
so a newer local version can disagree with it.

## Headless and real hardware

Software rendering under `xvfb-run` is fine for logic and determinism checks
([building.md](building.md#headless-runs)), but it can miss bugs that only a real
GPU backend or a console exposes. For changes that touch rendering, look at it
on a real display too, and say in the PR what you ran it on.
