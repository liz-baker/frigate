# ROM extraction library

`src/extract` (CMake target `frigate_extract`) turns a GoldenEye ROM into the four O2R archives
(`goldeneye_textures.o2r`, `goldeneye_objects.o2r`, `goldeneye_geometry.o2r`, `goldeneye_audio.o2r`) without Python
or the decomp. It depends only on zlib, so it builds without libultraship. Tracked in #17.

## Running it

    just extract-cli "romdata/GoldenEye 007 (USA).z64" archives/
    frigate-extract <rom.z64> <output-dir> [--region u|e|j]

The ROM must be a big-endian `.z64` dump of an unmodified release; the region is detected from its SHA-1. The packer
stages are being ported one by one; an archive whose packers are not ported yet is written empty. Ported so far: the
texture bank and image packers, the audio packer, the object tables, and the model, level geometry and level data packers (below). The Rareware logo, gun barrel, HUD and sky packers need the US ROM's tables
and fail with `RegionUnsupported` on the others.

## API

Everything is in `extract/Extractor.h`, namespace `Frigate::Extract`.

- `Extractor::Run(Options)` returns a `Result`: an `Error` (code plus a message fit to show on screen), the detected
  `Region`, and the archive paths. Options carry the ROM path, output directory, an optional expected region, a
  `ProgressCallback` and a `const std::atomic<bool>* cancel`.
- Archives are written as `<name>.o2r.tmp` and renamed when whole. A failure or cancellation removes everything the
  run produced.
- Output names carry no region, so one output directory holds one region's set.

### Adding a packer

A packer is a `Stage` (or `MakeStage(name, archive, lambda)`): it gets a `StageContext` (region, the whole ROM,
`layout()`, `RomRange()`, `ReadFile()` which inflates compressed entries, `Report()` for progress, `Cancelled()`)
and the archive's `O2rWriter`. Register it in `RegisterDefaultStages()` in `Extractor.cpp`, replacing that archive's
placeholder. Stages run in registration order within an archive; an archive with no stages is not written. Check
`Cancelled()` in long loops.

## Pieces

- `Region.h`: region info (hash, image shift), `DetectRegion*()`, and `IsUsRom()` which rejects any file not exactly
  12 MiB before hashing it.
- `RomLayout.h`: `GetRomLayout(region)` gives the file list and `RegionConstants` (fixed offsets the packers carry);
  `ImageTable()` plus `ImageRomOffset(region, i)` give the image list. `RegionConstants::known` is false for regions
  whose values are not worked out.
- `Inflate.h`: `Inflate1172()` (2-byte `0x1172` header then raw DEFLATE; the 5-byte `0x1173` variant is accepted).
- `O2rWriter.h`: zip container and resource headers, matching `tools/o2r_packer/o2r_writer.py`
  (see `formats/o2r-resource-header.md`).

`RomLayoutData.cpp` and `ImageDefData.cpp` (the image names, hit surface types and detail-tile fields of
`images.def`) are generated from a decomp checkout; they are committed, so building needs no decomp. See
"Generated tables" below.

### Texture and image packers

`TexturePackers.h` registers them. `Texture.h` decodes the texture bitstream (`formats/goldeneye-texture-bitstream.md`),
`TextureMip.h` builds the mip chains (`formats/goldeneye-texture-mip-chain.md`) and `DisplayListEmit.h` walks and
re-emits display lists (`formats/goldeneye-display-list-resource.md`).

| stage | archive | members |
|-------|---------|---------|
| Textures | textures | `textures/<name>`, `.pal` and `.mip` companions |
| Rareware logo | geometry | `rarewarelogo/...` |
| Gun barrel | geometry | `eyeintro/sight`, `eyeintro/blood` |
| HUD images | geometry | `hud/...` |
| Sky images | geometry | `sky/img/<n>` |
| Texture hits | geometry | `textures/hits` |

The textures stage decodes one image at a time straight from the ROM, so it holds the ROM and one decoded image (about 18 MiB resident in all).

### Model, level geometry and level data packers

`ModelPackers.h` registers them. `ModelContainer.h` unwraps model files and bg files (`formats/goldeneye-containers.md`),
`ModelTree.h` parses a model's node tree and encodes the `GEMD` and `GESK` payloads
(`formats/goldeneye-model-nodes.md`, `formats/goldeneye-model-tree-resource.md`,
`formats/goldeneye-model-skeleton-resource.md`), `Stan.h` converts stan files (`formats/goldeneye-stan-resource.md`), and
`DisplayListEmit.h` re-emits the lists (`ReemitLists()`).

| stage | archive | members |
|-------|---------|---------|
| Models | objects | `models/<chr\|gun\|prop>/<name>/...`: `vtx/`, `dl/`, `img/`, `fx/`, `tree`, `skeleton` |
| Level data | geometry | `obseg/stan`, `obseg/text`, `obseg/setup`, `obseg/brief`, `font/<bank>` |
| Level geometry | geometry | `bg/<stage>/header`, `bg/<stage>/room<NN>/vtx`, `.../dl` |
| Water lists | geometry | `bg/water/<image>` (`formats/goldeneye-water-lists.md`) |

Each model, and each bg room, is decoded, written and freed before the next, so the stages add only one model or room
to the ROM's 12 MiB. A model's switch and texture counts, skeleton name and bounding radius, and the skeleton joint
lists, are the decomp's `MODELFILEHEADER` and `JOINTLIST` source tables; `ModelTablesData.cpp` holds them, generated by
`tools/gen_model_tables.py`. Only the US ROM's tables exist, so the three stages fail with `RegionUnsupported` on
the others.

### Audio packer

`AudioBank.h`, `AudioPack.h`: the audio stage (`audio/sfx`, `audio/instruments`, `audio/music`, and the
  `audio/sfx_names` and `audio/music_names` blobs), ported from `ge_audio.py` and `pack_audio.py`.
  `AudioNamesData.cpp` holds the decomp's `SFX_ID` and `MUSIC_TRACKS` names and is generated (below).

### Object tables

`ObjectTables.h` registers one stage, "Object tables", in the objects archive. It writes the tables the game reads as
text blobs, byte for byte what the Python packers wrote:

| member | from | ported from |
|--------|------|-------------|
| `chr/records`, `chr/hats` | generated chr model records and hat placements, model header radii | `pack_chr_records.py` |
| `prop/models` | generated prop records, plus each model's bounding box read from its ROM file | `pack_prop_models.py` |
| `weapon/stats` | generated weapon table of the region | `pack_weapon_stats.py` |
| `chr/combat` | generated hit reaction, firing and explosion tables, forces of impact | `pack_chr_combat.py` |
| `monitor/lists` | generated monitor command lists | `pack_monitor_lists.py` |
| `animations/table` | generated animation ids | `pack_animation_table.py` |
| `ramrom/table`, `ramrom/<demo>` | generated demo table, recordings read from the ROM | `pack_ramrom.py` |
| `ai/globals` | `g_GlobalAILists` read from the ROM's data segment at the region's offsets | `pack_ai_lists.py` |

The prop bounding boxes come from the first bounding box node `chrobjGetBboxFromObjFile` finds; `FindModelBounds` walks
only the nodes it needs, so it does not validate the rest of the tree. Formats: `formats/goldeneye-{chr-records,prop-models,weapon-stats,chr-combat,animation-table,monitor-lists,ai-lists,
ramrom}.md`. The stage needs the region's data segment offsets, so it reports `RegionUnsupported` on the Europe and
Japan ROMs until those are mapped (`RegionConstants::known`).

## Generated tables

Everything the packers took from the decomp's C source is generated at development time and committed, so building,
CI and extraction on a device never need the decomp. `tools/gen_tables.py` (`just gen-tables`, needs `external/007`)
writes them:

| file | holds |
|------|-------|
| `src/extract/RomLayoutData.cpp`, `ImageDefData.cpp` | file lists, image tables and image definitions (by `tools/gen_rom_layout.py`, which `gen_tables.py` runs) |
| `src/extract/AudioNamesData.cpp` | `SFX_ID` and `MUSIC_TRACKS` names |
| `src/extract/ModelHeaderData.cpp` | the `MODELFILEHEADER` counts, skeleton and radius of every chr, gun and prop model |
| `src/extract/ChrRecordData.cpp`, `PropRecordData.cpp` | chr model records (`BODY_`/`HEAD_` names), hat placements, prop records (`PROP_` names) |
| `src/extract/WeaponStatsData.cpp` | weapons (`ITEM_` and ammo names), per region |
| `src/extract/ChrCombatData.cpp`, `AnimationTableData.cpp` | hit reactions, firing animations, explosion deaths, animation ids (clip names) |
| `src/extract/MonitorListData.cpp`, `RamromData.cpp` | monitor command lists with the macros expanded, the attract demo table |
| `src/level/AiCommands.h` | AI command opcodes and record layouts |

The symbolic names stay in the tables; `DecompTables.h` declares the row types. The sources are read with each region's
preprocessor conditionals resolved (`tools/tables/common.py` lists the build defines). Only the weapon stats differ by
region: `gunWeaponStats.inc.c` selects its `SCALEVAL`/`RATE` constants by `BUGFIX_R0` (US), `BUGFIX_R2` (Europe) or
`BUGFIX_R1` (Japan), so that table is generated three times. The generator fails if any other table comes out
different for another region. The Europe and Japan weapon tables are generated but not checked against a ROM.

`just check-tables` (`gen_tables.py --check`) regenerates in memory and fails on any difference; the build-validation
workflow runs it where the decomp is checked out.


## Tests

`ObjectTablesTest` runs the table builders on synthetic tables and model images and checks the generated tables agree
with each other (`tools/test_gen_tables.py` covers the generators). `ExtractTest` needs no ROM; its ROM-gated checks run when `FRIGATE_ROMS` (a path-separator list of files and
directories) or `romdata/` holds the US ROM. `ExtractWriterEquivalence` writes resources with the C++ and Python
writers and compares the member bytes.
`ExtractModelTest` covers the containers, the node tree, the stan converter and the model and level stages on
hand-built input, then compares sizes and CRCs against the Python packers' output on the real ROM.
