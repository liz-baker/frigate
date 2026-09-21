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

`frigate [--model models/chr/CarmourguardZ] [--distance N] [--textures] <archives...>`
(`just run --model ...`). Without `--model` it draws a solid fill.

## Known limits

- **Camera.** Distance is fixed (`--distance`, default 1000 units), so
  models of very different size need it adjusted by hand.
- **Node transforms.** Segment-3 `G_MTX` commands are turned into no-ops,
  so a model's parts draw stacked at the origin.
- **Textures.** By default texturing is off and surfaces draw with vertex
  shading only. `--textures` turns it on for lists that contain a
  `G_SETTEX`; those draws currently crash the OpenGL backend in
  `SetPerDrawUniforms`, which indexes a texture slot that was never set.

## Headless runs

The viewer renders under `xvfb-run` with `LIBGL_ALWAYS_SOFTWARE=1`; a
screenshot with ImageMagick's `import -window root` captures the frame. The
process ignores SIGTERM, so end it with SIGKILL.
