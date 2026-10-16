# ROM extraction library

`src/extract` (CMake target `frigate_extract`) turns a GoldenEye ROM into the four O2R archives
(`goldeneye_textures.o2r`, `goldeneye_objects.o2r`, `goldeneye_geometry.o2r`, `goldeneye_audio.o2r`) without Python
or the decomp. It depends only on zlib, so it builds without libultraship. Tracked in #17.

## Running it

    just extract-cli "romdata/GoldenEye 007 (USA).z64" archives/
    frigate-extract <rom.z64> <output-dir> [--region u|e|j]

The ROM must be a big-endian `.z64` dump of an unmodified release; the region is detected from its SHA-1. The packer
stages are all ported: the
texture bank and image packers, the audio packer, the object tables, the animation clips, and the model, level geometry and level data packers (below). The Rareware logo, gun barrel, HUD and sky packers need the US ROM's tables
and fail with `RegionUnsupported` on the others.

## First run

The game extracts its own archives. At startup, before anything loads them, `main()` checks the archive directory
(`--archive-dir`, else the app directory: `/wiiu/apps/frigate/` on the Wii U's SD card, elsewhere `$SHIP_HOME` or the
working directory). If any of the four standard archives is missing, empty or not a whole zip (no intact
end-of-central-directory record), the set counts as incomplete and the game extracts all four. A complete set is never
touched. Archive versions are not stamped, so an archive from an older extractor is only replaced when it is damaged.

The ROM is found in this order, stopping at the first match:

1. `--rom <path>`, if given. Nothing is searched; the file must still be the US ROM.
2. The app directory, then the current working directory (often the same folder, which is searched once).

Only regular files directly inside those folders are looked at (no subfolders). A file is read only when it is exactly
12 MiB, and is then identified by its SHA-1 against the pinned US hash (`abe01e4aeb033b6c0836819f549c791b26cfde83`),
whatever it is named. Unreadable files are skipped and reported. Europe and Japan ROMs found this way, byte-swapped
dumps and same-sized files with another hash are reported in the error but never extracted: only the US release is
supported yet.

`--no-extract` turns the whole flow off (the test recipes pass it where they use partial archive sets).

Progress and errors:

- Extraction runs on a worker thread through the library's progress callback and cancel flag, so the UI stays live.
- Desktop: a small SDL window (software renderer, no archives or `Ship::Context` needed) shows the stage and a bar;
  closing it cancels and removes anything written. Errors stay in that window until a key is pressed or 30 seconds pass.
- With no display (headless, `--play-sound`) or on the Wii U, there is no window; each stage and the outcome go to the
  log (`LUSLOG`). The Wii U shows nothing on screen yet.
- Failure messages say what was looked for and where, the library's own message, and for write errors where to look.
  Not found, a failed extraction and a cancel all make `frigate` exit 1 before the main window opens.
- Extraction writes each archive as `.tmp` and renames it, and a failed or cancelled run removes what it wrote, so a
  failed run leaves no archive of its own. If the process dies between renames the set is incomplete (an archive is
  missing) and the next start extracts again. Free space is checked first: the archives are about 15 MiB, and 40 MiB must be free.

On the desktop (Debug build) the extraction takes about 2 s and peaks near 20 MiB of memory (the ROM and one decoded image).

The pieces: `RomSearch.h` (`FindUsRom()`), `FirstRun.h` (`MissingArchives()`, `EnsureArchives()`, no libultraship) and
`src/platform/FirstRun.cpp` (the thread and the window). `FirstRunTest` covers them on fake files and, with the ROM,
end to end; `FirstRunEndToEnd` runs `frigate --play-sound` against an empty archive directory with the ROM in the
working directory.

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
| Animation clips | objects | `animations/<name>` (`formats/goldeneye-animation-clip-resource.md`) |
| Level data | geometry | `obseg/stan`, `obseg/text`, `obseg/setup`, `obseg/brief`, `font/<bank>` |
| Level geometry | geometry | `bg/<stage>/header`, `bg/<stage>/room<NN>/vtx`, `.../dl` |
| Water lists | geometry | `bg/water/<image>` (`formats/goldeneye-water-lists.md`) |

Each model, and each bg room, is decoded, written and freed before the next, so the stages add only one model or room
to the ROM's 12 MiB. A model's switch and texture counts, skeleton name and bounding radius, and the skeleton joint
lists, are the decomp's `MODELFILEHEADER` and `JOINTLIST` source tables; `ModelHeaderData.cpp` and
`SkeletonData.cpp` hold them (see Generated tables). The animation clips are ROM bytes: `PackAnimations.cpp` reads the
header words of each clip from the `animationtable_data` file at the offsets `AnimationClipData.cpp` lists, and the frames from
`animationtable_entries`. Only the US ROM's tables exist, so the model and clip stages fail with `RegionUnsupported` on
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
| `src/extract/ModelHeaderData.cpp`, `SkeletonData.cpp` | the `MODELFILEHEADER` counts, skeleton and radius of every chr, gun and prop model; the `JOINTLIST` joints of each skeleton |
| `src/extract/ChrRecordData.cpp`, `PropRecordData.cpp` | chr model records (`BODY_`/`HEAD_` names), hat placements, prop records (`PROP_` names) |
| `src/extract/WeaponStatsData.cpp` | weapons (`ITEM_` and ammo names), per region |
| `src/extract/ChrCombatData.cpp`, `AnimationTableData.cpp` | hit reactions, firing animations, explosion deaths, animation ids (clip names) |
| `src/extract/AnimationClipData.cpp` | each animation clip's name and header offset in the ROM's animation data file |
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
`ExtractModelTest` covers the containers, the node tree, the stan converter, the animation clips and the model and level stages on
hand-built input, then compares sizes and CRCs against the Python packers' output on the real ROM.
