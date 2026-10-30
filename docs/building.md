# Building and running

## What you need

- **A US GoldenEye 007 ROM**, big-endian `.z64`, unmodified. It is identified
  by its SHA-1, not its filename. Europe and Japan ROMs are not supported yet.
- **Tools:** git, CMake 3.24 or newer, a C++20 compiler, and
  [`just`](https://github.com/casey/just) (`brew install just`,
  `pacman -S just`). `just` is optional; every recipe is a thin wrapper over
  the commands shown under [Without just](#without-just).
- **Libraries:** SDL3, tinyxml2, nlohmann_json, spdlog, libpng, GLEW and
  libzip, plus Ninja if you want it.

CI's package lists are the reference for the library set:
`.github/workflows/apt-deps.txt` (Debian and Ubuntu) and
`.github/workflows/brew-deps.txt` (macOS: `brew install $(cat
.github/workflows/brew-deps.txt)`). On Arch, install the matching packages
(`sdl3`, `tinyxml2`, `nlohmann-json`, `spdlog`, `libpng`, `glew`, `libzip`).

## Get the source

```sh
git clone --recurse-submodules https://github.com/liz-baker/frigate
```

Already cloned without submodules? `just worktree-init` checks out
`external/libultraship-wiiu`, which the game needs. `external/007` (the
GoldenEye decompilation) is not read by the build: the tables the extractor
needs are generated from it ahead of time and committed. `just worktree-init
all` checks it out as well, which you need for `just gen-tables`,
`just check-tables`, `just verify-extract`, `just verify-texture-oracle`, and
for reading the decompiled source.

## Desktop (Linux, macOS)

```sh
just build                    # Debug; `just build Release` for an optimized build
cp /path/to/rom.z64 .         # or pass --rom <path> when you run it
just run
```

`just run` starts the game with `--archive-dir archives`. The first launch
extracts the four game archives from the ROM, with a progress window, and
later launches reuse them. Details, including where the ROM is searched for
and what counts as an incomplete archive set, are in
[extract.md](extract.md#first-run).

To extract ahead of time without starting the game:

```sh
just pack_archives "/path/to/GoldenEye 007 (USA).z64"     # writes archives/*.o2r
```

The desktop build also writes `f3d.o2r` (Fast3D's shaders, packed by
`tools/pack_shaders`) next to the executable. Keep the two together: the
renderer aborts without it. The Wii U build does not need it.

### Windows

CI builds with MSVC and vcpkg (`-DUSE_AUTO_VCPKG=ON`) on a manually triggered
job. It is not routinely exercised, so expect rough edges and file issues for
what you hit.

### Without just

```sh
git submodule update --init external/libultraship-wiiu
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release -DFRIGATE_REQUIRE_GAME=ON
cmake --build build --target frigate --parallel
./build/frigate --archive-dir archives
```

`FRIGATE_REQUIRE_GAME=ON` turns a missing libultraship into a configure error.
Without it, CMake quietly configures only the submodule-free targets (the
extraction library and its tools).

## Running

`just run [args]` passes its arguments to the game.

| Recipe or flag | Does |
|---|---|
| `just attract` | The whole title sequence: legal screen, logos, gun barrel, cast, then a random attract demo, round again |
| `just attract N` | Attract demo N (0 to 13) straight from its stage load |
| `--demos 0,1,2` | Play those demos in turn instead of at random |
| `--no-abort-on-mismatch` | Keep a demo playing when the random byte differs from the recording (it is still logged) |
| `--uncapped` | Drop the frame limit |
| `just multiplayer N [options]` | A 2 to 4 player game without the menus ([multiplayer-quick-start.md](multiplayer-quick-start.md)) |
| `just play_sound NAME` | Play one sound effect or music track and exit ([audio.md](audio.md)) |
| `--archive-dir DIR` | Where the `.o2r` archives are (default: the app directory) |
| `--rom PATH` | The ROM to extract from on first launch |
| `--no-extract` | Never extract, even if archives are missing |
| `--input-script "300-302:A,500-502:DD"` | Press buttons on given frames in place of a controller, so a headless run can drive the menus |

The app directory is `/wiiu/apps/frigate/` on a Wii U's SD card, and
elsewhere `$SHIP_HOME` or the working directory.

Press Esc to open the configuration menu ([menu.md](menu.md)).

## Tests

```sh
just test
```

builds everything and runs the C++ and Python unit tests under `ctest`. Tests
that need a ROM look in `romdata/` at the repository root, or in the directory
named by `FRIGATE_ROMS`, and report a skip when there is none; tests that need
the packed archives skip the same way. The checks
beyond unit tests are in [testing.md](testing.md).

## Wii U

Needs [devkitPro](https://devkitpro.org/) with the `wiiu-cmake` package
(`DEVKITPRO` defaults to `/opt/devkitpro`) and Ninja.

```sh
just wiiu_build                       # writes build-wiiu/frigate.wuhb (Debug)
WIIU_HOST=192.168.1.50 just wiiu_run  # build, send with wiiload, tail the UDP log
```

The console has to be sitting in the Homebrew Launcher or Aroma with wiiload
active. Only Debug builds emit the UDP debug log (port 4405). Put the ROM in
`/wiiu/apps/frigate/` on the SD card; the game extracts its archives there on
first launch.

## Headless runs

For CI or a machine with no display:

```sh
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a just attract 6 --uncapped
SDL_AUDIODRIVER=disk just play_sound rocket_launch    # audio to a raw file ([audio.md](audio.md))
```

Software rendering is slow and can differ from a real GPU, so a bug that
reproduces only on a real backend will not necessarily show up here.

A screenshot of the virtual display with ImageMagick's `import -window root`
captures a frame. The process ignores SIGTERM, so end it with SIGKILL.

## Developer builds and release builds

The launch arguments for testing (`--demo`, `--level`, `--multiplayer`,
`--play-sound`, `--input-script`, `--uncapped`, ...) and the code behind them
live in `src/dev/` and are left out of `Release` and `MinSizeRel` builds, which
accept only `--archive-dir`, `--rom` and `--no-extract`. The recipes in this
document that take those other arguments (`just attract`, `just multiplayer`,
`just play_sound`) need a Debug or RelWithDebInfo build.

## Troubleshooting

- **Configure fails with "libultraship" not found:** the submodule is not
  checked out. Run `just worktree-init`.
- **Aborts at start with a missing `f3d.o2r`:** run the executable from the
  directory it was built in, or copy `f3d.o2r` next to it.
- **Extraction says the ROM is unsupported:** only the unmodified US `.z64`
  works. Byte-swapped dumps (`.v64`, `.n64`) are reported but not extracted.
- **A new worktree builds slowly:** `just build` and `just test` reuse the main
  checkout's already-fetched dependency sources when it has them.
