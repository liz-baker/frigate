set shell := ["bash", "-euo", "pipefail", "-c"]

build_dir := "build"
out_dir := "archives"

# Points FetchContent at the main checkout's already-fetched dependency sources
# so a fresh worktree skips those clones; empty when the main checkout has none
# (fresh clone, CI). stb is a plain download in libultraship, not FetchContent.
fetch_flags := `main=$(dirname "$(git rev-parse --path-format=absolute --git-common-dir)"); for dep in imgui monocypher prism threadpool; do [ -d "$main/build/_deps/$dep-src" ] && printf -- '-DFETCHCONTENT_SOURCE_DIR_%s=%s ' "${dep^^}" "$main/build/_deps/$dep-src"; done; true`

[private]
default:
    @just --list

# Check out the submodules in this worktree, borrowing objects from the main
# checkout so no network is needed when the pinned commit exists there. Always
# does libultraship; `all` also does the GoldenEye decomp (~85 MB, only needed
# to read decomp data). Idempotent. Run first in any new worktree.
worktree-init what="":
    #!/usr/bin/env bash
    set -euo pipefail
    names=(libultraship-wiiu)
    case "{{what}}" in
        "") ;;
        all) names+=(007) ;;
        *) echo "usage: just worktree-init [all]" >&2; exit 1 ;;
    esac
    main=$(dirname "$(git rev-parse --path-format=absolute --git-common-dir)")
    here=$(git rev-parse --show-toplevel)
    for name in "${names[@]}"; do
        if [ "$main" = "$here" ] || [ ! -e "$main/external/$name/.git" ]; then
            git submodule update --init "external/$name"
        else
            git submodule update --init --reference "$main/external/$name" "external/$name"
        fi
    done

# Configure and build the desktop game with the developer launch arguments
# (FRIGATE_DEV_TOOLS). Debug by default -- pass `type=Release` for an optimized
# build once things are working.
build type="Debug":
    cmake -S. -B{{build_dir}} -DCMAKE_BUILD_TYPE={{type}} -DFRIGATE_REQUIRE_GAME=ON {{fetch_flags}}
    cmake --build {{build_dir}} --target frigate --parallel

# Build and run the C++ and Python unit tests.
test:
    cmake -S. -B{{build_dir}} -DCMAKE_BUILD_TYPE=Debug -DFRIGATE_REQUIRE_GAME=ON {{fetch_flags}}
    cmake --build {{build_dir}} --parallel
    ctest --test-dir {{build_dir}} --output-on-failure

# Extract the textures, objects, geometry and audio archives from a US ROM into archives/ with the C++
# extractor (the same code the game runs on first launch). `ref-archives` builds the Python reference set.
pack_archives rom: (extract-cli rom out_dir)

# Build and run the standalone ROM extractor (docs/extract.md): archives for the ROM into out.
extract-cli rom out=out_dir:
    cmake -S. -B{{build_dir}} -DCMAKE_BUILD_TYPE=Debug {{fetch_flags}}
    cmake --build {{build_dir}} --target frigate-extract --parallel
    ./{{build_dir}}/frigate-extract "{{rom}}" "{{out}}"

# Build the Python reference archives for a US ROM into build/ref/ (what the C++ extractor must match).
ref-archives rom:
    mkdir -p {{build_dir}}/ref
    python3 tools/o2r_packer/build_textures_o2r.py --rom "{{rom}}" --output {{build_dir}}/ref/goldeneye_textures.o2r
    python3 tools/o2r_packer/build_objects_o2r.py --rom "{{rom}}" --output {{build_dir}}/ref/goldeneye_objects.o2r
    python3 tools/o2r_packer/build_geometry_o2r.py --rom "{{rom}}" --output {{build_dir}}/ref/goldeneye_geometry.o2r
    python3 tools/o2r_packer/build_audio_o2r.py --rom "{{rom}}" --output {{build_dir}}/ref/goldeneye_audio.o2r

# Run frigate-extract on a ROM into build/ext/. Builds only the extractor, so it works without the game's
# submodules.
ext-archives rom:
    cmake -S. -B{{build_dir}} -DCMAKE_BUILD_TYPE=Debug {{fetch_flags}}
    cmake --build {{build_dir}} --target frigate-extract --parallel
    rm -rf {{build_dir}}/ext
    ./{{build_dir}}/frigate-extract "{{rom}}" {{build_dir}}/ext

# Build both archive sets and compare them (docs/extract-verification.md). Extra arguments go to
# tools/compare_archives.py, e.g. `just verify-extract rom.z64 --prefix textures/` or `--allow-missing-in-ext`.
verify-extract rom *args:
    just ref-archives "{{rom}}"
    just ext-archives "{{rom}}"
    python3 tools/compare_archives.py {{build_dir}}/ref {{build_dir}}/ext --covered tests/extract/covered_prefixes.txt {{args}}

# Check the extractor's texture decoder against the game's own decompression code, built from the decomp
# (docs/extract-verification.md). Prints a per-group count and exits 1 on any difference.
verify-texture-oracle rom:
    cmake -S. -B{{build_dir}} -DCMAKE_BUILD_TYPE=Debug {{fetch_flags}}
    cmake --build {{build_dir}} --target frigate_extract_texture_oracle_tests --parallel
    FRIGATE_ROMS="{{rom}}" ./{{build_dir}}/tests/extract/frigate_extract_texture_oracle_tests

# Regenerate the committed tables read from the decomp (src/extract/*Data.cpp, src/level/AiCommands.h).
gen-tables:
    python3 tools/gen_tables.py --decomp external/007

# Fail if a committed table differs from a fresh reading of the decomp (what CI runs).
check-tables:
    python3 tools/gen_tables.py --decomp external/007 --check

# Run the game (needs archives/ -- see pack_archives). `--input-script
# "300-302:A,500-502:DD+X=-70"` presses buttons on given frames in place of
# the controller (see src/platform/InputScript.h), so a headless run can drive
# the menus.
run *args:
    ./{{build_dir}}/frigate --archive-dir {{out_dir}} {{args}}

# Run the attract sequence the way the game does: legal screen, logos, gun
# barrel, cast, then a random attract demo that fades back to the title, and
# round again. With a leading demo number (0-13, the ramrom table order) play
# just that demo straight from the stage load. Other arguments go to the
# game: `--demos 0,1,2` plays those demos in turn instead of the random pick,
# `--no-abort-on-mismatch` plays a demo past a random-byte mismatch with the
# recording instead of fading out as the original does, `--uncapped` drops the
# frame limit.
attract *args:
    #!/usr/bin/env bash
    set -euo pipefail
    set -- {{args}}
    if [[ "${1:-}" =~ ^[0-9]+$ ]]; then
        index=$1
        shift
        just run --demo "$index" "$@"
    else
        just run "$@"
    fi

# Start a multiplayer game without the menus, for 2 to 4 players, with the
# controller ports it needs forced connected. Other arguments choose the
# settings: `just multiplayer 3 --stage facility --handicaps 5,3,7` (see
# docs/multiplayer-quick-start.md).
multiplayer players="2" *args:
    just run --multiplayer --players {{players}} {{args}}

# Run the headless multiplayer checks (docs/multiplayer-verification.md): the
# core cases at 2, 3 and 4 players, or the named cases and groups
# (`just mp_test stages`, `just mp_test walk-alone`).
mp_test *cases:
    cmake --build {{build_dir}} --target frigate_multiplayer_harness --parallel
    ./{{build_dir}}/tests/game/frigate_multiplayer_harness {{out_dir}} {{cases}}

# Play one sound effect through the audio backend and exit, by name (a
# case-insensitive SFX_ID name, `_SFX` optional), by number, or as audio/sfx/<either>.
# A music track name (M_INTRO, `M_` optional, or audio/music/<name or number>)
# plays that track in a loop until Ctrl-C; `--play-seconds <n>` stops it after n seconds.
play_sound sound *args:
    ./{{build_dir}}/frigate --play-sound "{{sound}}" --archive-dir {{out_dir}} {{args}}

# Cross-compile for Wii U and package build-wiiu/frigate.wuhb. Debug by
# default, since only Debug builds emit the UDP log.
wiiu_build type="Debug":
    #!/usr/bin/env bash
    set -euo pipefail
    export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
    [[ -f "$DEVKITPRO/cmake/WiiU.cmake" ]] || { echo "no $DEVKITPRO/cmake/WiiU.cmake: install devkitPro's wiiu-cmake or set DEVKITPRO" >&2; exit 1; }
    cmake -S. -Bbuild-wiiu -GNinja -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/WiiU.cmake" -DCMAKE_BUILD_TYPE={{type}}
    cmake --build build-wiiu --target frigate_wuhb --parallel

# Send the .wuhb to a Wii U with wiiload and tail its UDP log (set WIIU_HOST).
wiiu_deploy *args:
    scripts/wiiu-deploy.sh {{args}}

# Build, then deploy.
wiiu_run *args: wiiu_build
    scripts/wiiu-deploy.sh {{args}}
