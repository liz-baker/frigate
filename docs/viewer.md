# Viewer glue

`src/viewer/` (target `frigate_viewer`) is throwaway scaffolding for
exercising the asset pipeline before the scene graph and game loop exist.
Nothing outside it should depend on it except `src/main.cpp`. Removing the
directory, the CMake target and the `main.cpp` call is the whole cleanup.

| File | What it does | Retired by |
|---|---|---|
| `GbiF3dex.h` | Builds F3DEX-numbered commands (`G_MTX`, `G_MOVEMEM`, geometry and othermode words) by hand, since the stock macros emit F3DEX2 opcodes | Hand-built lists going away; candidate for upstreaming if it keeps growing |
| `Camera.*` | Fixed projection, modelview and viewport | https://github.com/liz-baker/frigate/issues/39 |
| `ModelStub.*` | Lists a model's `<model>/dl/*` resources and calls each by hash, untransformed | https://github.com/liz-baker/frigate/issues/12 |
| `Viewer.*` | Frame loop, framebuffer clear | The real game loop |

## Usage

`frigate [--model models/chr/CarmourguardZ] [--distance N] [--no-textures] <archives...>`
(`just run --model ...`). Without `--model` it draws a solid fill.

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
  samples with no texture ever imported. Requests the load-time expansion
  leaves as stored (paletted and 32-bit images, other request types) count
  as not loaded, so in the US ROM's models this covers all chr and gun
  lists and most props: only 7 of 512 models have a list that loads
  a texture. Lifting that is
  https://github.com/liz-baker/frigate/issues/29.

## Checking a texture

`--texture textures/<name>` draws one non-paletted texture 1:1 through the
same `G_SETTEX` expansion a model list goes through, instead of a model.
`tools/verify_texture_render.py` runs it headless and compares the frame with
the image decoded straight from the archive (`just verify_texture <names>`).

## Headless runs

The viewer renders under `xvfb-run` with `LIBGL_ALWAYS_SOFTWARE=1`; a
screenshot with ImageMagick's `import -window root` captures the frame. The
process ignores SIGTERM, so end it with SIGKILL.
