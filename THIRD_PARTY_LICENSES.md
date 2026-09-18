# Third-party attributions

This file tracks code adapted into this repository from other projects,
per `CLAUDE.md`'s licensing section — see there for the general policy.
This repo's own code otherwise remains MIT-licensed (`LICENSE`).

## Audio microcode interpreter (`src/audio/AudioUcode.cpp`, `AudioUcode.h`)

Adapted from `goldeneye-pc-port`'s software reimplementation of GoldenEye
007's RSP audio ucode (`aspMain`), which was never decompiled and has no
portable source of its own to port from directly:

- Source: https://github.com/jkdansereau/goldeneye-pc-port
- Files: `port/src/mixer.c`, `port/include/mixer.h`
- License: MIT (repository root `LICENSE`, copyright James Dansereau).
  These files live outside `port/fast3d/`, so they fall under
  `goldeneye-pc-port`'s own top-level MIT license, not the separately
  MIT-licensed Emill/MaikelChan `fast3d`/`gfx_pc.cpp` engine that project
  also vendors.

What was carried over and what changed:

- The DSP algorithms (ADPCM decode, linear resample against the N64's
  64-phase resample table, linear envelope mixing, dry/wet gain,
  interleave, DMEM-buffer mix) are ported as-is — this is the actual
  Nintendo/SGI ucode math, not something specific to the PC port.
- The DMEM-addressing/context layer (`aSetBuffer`'s persistent
  in/out/count/dryR/wetL/wetR context, consumed by the opcodes that don't
  carry explicit addresses) is ported as-is — it reflects GE's own ucode
  ABI, reconstructed by the original authors from libultra call sites, not
  something this port invented.
- The `osPhysicalToVirtual(dramAddr)` indirection is replaced with direct
  host pointers at the opcode boundary (`LoadBuffer`/`SaveBuffer`/state
  pointers take `void*`/typed pointers instead of `u32` addresses). This
  repo has no libultra segment/physical-address system yet (that's part of
  the not-yet-started decomp build integration, `frigate` roadmap item 3),
  so there is nothing for the original indirection to resolve against;
  callers are expected to resolve real pointers themselves once that
  integration exists.
- The `aXxx(pkt, ...)` macro layer that let `goldeneye-pc-port` intercept
  the decomp's existing `PR/abi.h` call sites transparently is dropped —
  this repo's decomp integration hasn't happened yet, so there are no such
  call sites to intercept. `AudioUcode`'s functions are ordinary C++ calls
  instead; reintroducing an equivalent macro shim (if still useful) is
  scoped to that future integration work, not this change.
- The temporary diagnostic instrumentation the original carries
  (`GE_MIXERTRACE`, `GE_DMEMWIPE`, `GE_NOWET`, `GE_VOICEDUMP` and their
  associated file logging, added there to chase a specific reverb/voice
  bug) is not ported — it's dead weight without the bug it was chasing,
  and this repo can add its own instrumentation if a similar need comes up.

## Texture-bank packer (`tools/o2r_packer/pack_textures.py`)

Adapted from `goldeneye-pc-port`'s `images.def` texture-bank tooling:

- Source: https://github.com/jkdansereau/goldeneye-pc-port
- Files: `scripts/make/generate_imagelist.py`
- License: MIT (repository root `LICENSE`, copyright James Dansereau).

What was carried over and what changed:

- The `images.def` `IMAGE(NAME, SIZE, ...)` parsing logic
  (`parse_images_def`) is ported as-is — this is bare format knowledge
  (the macro's field layout), not something specific to how the PC port
  uses it.
- The output step is new, not ported: instead of writing an offset/size
  CSV describing a single concatenated ROM-style blob (the PC port's
  `generate_imagelist`), this writes each named image as a real
  `Fast::Texture` O2R resource via `O2rWriter`, keyed by name.
- Pixel-format decoding (`tools/o2r_packer/ge_texture.py`, frigate issue
  #18) is not an adaptation of `goldeneye-pc-port` — GoldenEye's own
  texture-compression scheme is decomp-inherited code there too (see
  that repository's own `NOTICE` file, which carves `src/`/`include/`
  out of its MIT grant), so it carries the same unlicensed-upstream
  restriction as `external/007` itself. `ge_texture.py` is a fresh-wording
  reimplementation from bare algorithmic facts, not a licensed
  adaptation, and has no entry here for that reason — see its own module
  docstring for the full sourcing note.
