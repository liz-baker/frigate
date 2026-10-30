# Profiling the Wii U build

Wii U Debug builds (`just wiiu_build`, the default) include a frame profiler and
a set of experiment switches in the renderer. Release builds carry none of it.
The profiler is libultraship's `FAST3D_PROFILE` option, which frigate turns on for
Wii U Debug builds; the code is `include/fast/profiler.h` and `src/fast/profiler.cpp`
there.

## Reading the log

Once a second the profiler writes lines starting with `[prof]` to the log file
(`logs/frigate.log` in the app directory on the SD card) and to the UDP log
(`just wiiu_deploy` streams it). They show:

- the frame rate, frame time (average and worst) and how many frames took one,
  two, three or more refreshes at 60 Hz, plus frames the renderer dropped;
- `ms/frame` per stage. Times are exclusive and add up to the frame: a stage's
  time leaves out the stages it calls. `OTHER` is everything no stage claims;
  `events+input`, `menu_tick`, `dl_build` and `wait_for_frame` are the game's
  own stages (the stage and menu loops, not the attract demo). `dl_build` is
  split into the stage's render pass: `r_setup`, `r_sky`, `r_props+chr_tick`
  (room visibility and each on-screen prop's tick), `r_ticks`, `r_room_draw`,
  `r_effects`, `r_guns_watch` and `r_hud`; `onscreen_props` counts the props;
- the display-list commands `RUN` spends most of its own time on, with ms and
  calls per frame;
- counts per frame: triangles seen, rejected by clipping or culling, and
  emitted; vertices; flushes and why (`FLUSH_*`); draw calls; texture imports;
  texture KB uploaded; and CPU waits on the GPU by cause (`SYNC_*`).

## Experiments

An experiment removes or changes one piece of the frame, so the change in the
numbers is that piece's cost. Put them in `args.txt` in the app directory:

    --exp skip_draw --exp no_blend

| Name | Effect |
|---|---|
| `skip_draw` | never call the backend's draw: removes the GPU's work and the draw-call cost |
| `skip_tris` | drop every triangle before it is processed |
| `tri_limit=N` | emit at most N triangles a frame |
| `no_blend` | never enable alpha blending |
| `no_depth` | never enable the depth test or depth writes |
| `no_fog` | leave fog out of every shader |
| `point_filter` | point sampling for every texture |
| `measure_gpu` | wait for the GPU after the display list, so `GPU_SYNC` shows the GPU's time |
| `swap_interval=N` | override the swap interval (1 refreshes of 60 Hz per frame) |
| `res=X` | internal resolution multiplier, read at start |
| `skip_effects` | the stage's effects (smoke, sparks, decals) are not drawn |
| `skip_sky` | the sky is not drawn |
| `skip_guns_hud` | the guns, watch and HUD are not drawn |
| `skip_ticks` | skip the alarm, AI sound and prop-list defragmentation ticks |
| `cycle=N` | step through the experiments above every N seconds, alternating with a baseline step; each step is labelled `cycle step K: <name>` in the log |

Stand still in the slow spot for a full cycle (`--exp cycle=4` is 26 steps, 104
seconds) and compare each step with the baseline step beside it. Experiments that remove
work (`skip_*`, `tri_limit`) change what is on screen.
