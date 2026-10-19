#!/usr/bin/env python3
"""Regenerates the committed tables that are read from the GoldenEye decomp, and checks that they are current.

    tools/gen_tables.py [--decomp external/007]            write the tables
    tools/gen_tables.py [--decomp external/007] --check    fail if a committed table differs from a fresh one

Everything it writes is source in this repository (`src/extract/*Data.cpp`, `src/level/AiCommands.h`, `src/audio/SfxIds.h`), so building
and extracting need no decomp checkout; only regenerating does. The tables keep the decomp's symbolic names (PROP_,
ITEM_, animation and list names, ...).

The preprocessor conditionals of the sources are resolved for each region's build (tools/tables/common.py has the
defines). Only the weapon stats differ between regions: gunWeaponStats.inc.c picks its SCALEVAL and RATE constants
by BUGFIX_R0 (US), BUGFIX_R2 (Europe) or BUGFIX_R1 (Japan), so that table is generated for all three. The other
tables have no region-dependent content in the decomp.

The ROM file lists and image definitions are produced by tools/gen_rom_layout.py, which this runs.
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from tables import ai_commands, audio, combat, models, monitors, weapons  # noqa: E402
from tables.common import REGIONS, Decomp, Output, TableError  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
ROM_LAYOUT_OUTPUTS = ("src/extract/RomLayoutData.cpp", "src/extract/ImageDefData.cpp")


def generate_region(decomp_root: Path, region: str):
    """The tables that do not vary by region, read with `region`'s preprocessor conditionals resolved."""
    decomp = Decomp(decomp_root, region)
    return [
        audio.generate_audio_names(decomp),
        audio.generate_sfx_ids(decomp),
        models.generate_model_headers(decomp),
        models.generate_skeletons(decomp),
        models.generate_chr_records(decomp),
        models.generate_prop_models(decomp),
        combat.generate_chr_combat(decomp),
        combat.generate_animation_table(decomp),
        combat.generate_animation_clips(decomp),
        monitors.generate_monitor_lists(decomp),
        monitors.generate_ramrom(decomp),
    ]


def generate(decomp_root: Path):
    """Every generated file as an Output, except the ROM layout files."""
    outputs = generate_region(decomp_root, "us")
    # The sources have no region-dependent content in these tables today; fail rather than ship the US reading for
    # a region where a decomp update made it differ.
    for region in REGIONS[1:]:
        for us, other in zip(outputs, generate_region(decomp_root, region)):
            if us.text != other.text:
                raise TableError(f"{us.path} differs between the US and {region} builds; it needs a per-region table")
    return outputs + [weapons.generate_weapon_stats(decomp_root), ai_commands.generate_ai_commands(decomp_root)]


def run_rom_layout(decomp_root: Path, directory: Path):
    """Runs tools/gen_rom_layout.py with its outputs under `directory`; returns them as Outputs."""
    outputs = [directory / "RomLayoutData.cpp", directory / "ImageDefData.cpp"]
    subprocess.run(
        [
            sys.executable, str(REPO / "tools" / "gen_rom_layout.py"), "--decomp", str(decomp_root),
            "--output", str(outputs[0]), "--defs-output", str(outputs[1]),
            "--hit-types", str(REPO / "src" / "extract" / "ImageDefs.h"),
        ],
        check=True,
    )
    return [Output(relative, path.read_text()) for relative, path in zip(ROM_LAYOUT_OUTPUTS, outputs)]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--decomp", default=REPO / "external" / "007", type=Path)
    parser.add_argument("--check", action="store_true", help="compare with the committed files instead of writing")
    args = parser.parse_args()

    if not (args.decomp / "src" / "bondconstants.h").exists():
        raise SystemExit(f"{args.decomp} is not a GoldenEye decomp checkout (git submodule update --init external/007)")
    try:
        outputs = generate(args.decomp)
    except TableError as error:
        raise SystemExit(f"error: {error}")
    with tempfile.TemporaryDirectory() as scratch:
        outputs += run_rom_layout(args.decomp, Path(scratch))

    stale = []
    for output in outputs:
        path = REPO / output.path
        if args.check:
            if not path.exists() or path.read_text() != output.text:
                stale.append(output.path)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(output.text)
    if args.check:
        if stale:
            print("out of date (run `just gen-tables`):\n  " + "\n  ".join(stale), file=sys.stderr)
            return 1
        print(f"{len(outputs)} generated files are current")
    return 0


if __name__ == "__main__":
    sys.exit(main())
