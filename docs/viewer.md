# Viewer glue

`src/viewer/` (target `frigate_viewer`) is throwaway scaffolding for
exercising the asset pipeline before the scene graph and game loop exist.
Nothing outside it should depend on it except `src/main.cpp`. Removing the
directory, the CMake target and the `main.cpp` call is the whole cleanup.

| File | What it does | Retired by |
|---|---|---|
| `GbiF3dex.h` | Builds F3DEX-numbered commands (`G_MTX`, `G_MOVEMEM`, geometry and othermode words) by hand, since the stock macros emit F3DEX2 opcodes | Hand-built lists going away; candidate for upstreaming if it keeps growing |
| `Camera.*` | Fixed projection, modelview and viewport | Not planned; kept for `--model` asset checks |
| `ModelStub.*` | Walks a model's `<model>/tree` resource, supplies its matrices in segment 3 and calls each list by hash | Not planned; kept for `--model` asset checks |
| `LevelView.*` | Loads a stage, sets a fixed camera in one of its rooms and draws the rooms the visibility pass finds | Not planned; kept for `--level`/`--room` checks |
| `Viewer.*` | Frame loop, framebuffer clear | The real game loop |

## Usage

`frigate [--model models/chr/CarmourguardZ] [--distance N] [--no-textures] <archives...>`
(`just run --model ...`). Without `--model` it draws a solid fill.

## Drawing a stage

`just run --level bg_dam --room 1 [--camera x,y,z] [--look x,y,z]` draws a
stage's visible rooms from a fixed camera (`bg_*` is the stage's directory in
the geometry archive, e.g. `bg_sev`, `bg_arch`). The camera defaults to the
centre of `--room`, looking down -Z; positions are in the game's world units.

Without `--room`, `--camera` or `--look` the controller plays the stage from
its spawn. `--control-style 1.1` to `2.4` picks the control style (default
1.1). The two-controller styles 2.1 to 2.4 read the second controller (port 2);
with only one connected they play as the original does, the second pad's
controls idle.

## Known limits

- **Camera.** Distance is fixed (`--distance`, default 1000 units), so
  models of very different size need it adjusted by hand.
- **Node transforms.** Segment-3 `G_MTX` commands are turned into no-ops,
  so a model's parts draw stacked at the origin.
- **Textures.** Texturing is on by default; `--no-textures` draws
  everything with vertex shading only. A list samples a texture only if it
  loads one before its first draw. Every other list has its own
  `G_SETCOMBINE` and `G_TEXTURE` commands rewritten to shade-only, because
  a list carries its own combiner and the backend crashes when a shader
  samples with no texture ever imported. A request the load-time expansion
  can't resolve is left as stored and counts as not loaded.

## Checking a texture

`--texture textures/<name>` draws one non-paletted texture 1:1 through the
same `G_SETTEX` expansion a model list goes through, instead of a model.
`tools/verify_texture_render.py` runs it headless and compares the frame with
the image decoded straight from the archive (`just verify_texture <names>`).

## Headless runs

The viewer renders under `xvfb-run` with `LIBGL_ALWAYS_SOFTWARE=1`; a
screenshot with ImageMagick's `import -window root` captures the frame. The
process ignores SIGTERM, so end it with SIGKILL.
