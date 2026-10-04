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

# Configure and build the desktop viewer/game. Debug by default -- pass
# `type=Release` for an optimized build once things are working.
build type="Debug":
    just worktree-init
    cmake -S. -B{{build_dir}} -DCMAKE_BUILD_TYPE={{type}} -DFRIGATE_REQUIRE_GAME=ON {{fetch_flags}}
    cmake --build {{build_dir}} --target frigate --parallel

# Build and run the C++ and Python unit tests.
test:
    just worktree-init
    cmake -S. -B{{build_dir}} -DCMAKE_BUILD_TYPE=Debug -DFRIGATE_REQUIRE_GAME=ON {{fetch_flags}}
    cmake --build {{build_dir}} --parallel
    ctest --test-dir {{build_dir}} --output-on-failure

# Pack textures, objects, geometry and audio archives from a US ROM into archives/.
pack_archives rom:
    just worktree-init all
    mkdir -p {{out_dir}}
    python3 tools/o2r_packer/build_textures_o2r.py --rom "{{rom}}" --output {{out_dir}}/goldeneye_textures.o2r
    python3 tools/o2r_packer/build_objects_o2r.py --rom "{{rom}}" --output {{out_dir}}/goldeneye_objects.o2r
    python3 tools/o2r_packer/build_geometry_o2r.py --rom "{{rom}}" --output {{out_dir}}/goldeneye_geometry.o2r
    python3 tools/o2r_packer/build_audio_o2r.py --rom "{{rom}}" --output {{out_dir}}/goldeneye_audio.o2r

# Draw the briefcase gun, textured (five paletted images).
demo:
    just viewer --model models/gun/GbriefcaseZ --distance 300

# Run the game (needs archives/ -- see pack_archives).
run *args:
    ./{{build_dir}}/frigate {{out_dir}}/goldeneye_textures.o2r {{out_dir}}/goldeneye_objects.o2r {{out_dir}}/goldeneye_geometry.o2r {{out_dir}}/goldeneye_audio.o2r {{args}}

# Run the attract sequence the way the game does: legal screen, logos, gun
# barrel, cast, then a random attract demo that fades back to the title, and
# round again. With a leading demo number (0-13, the ramrom table order) play
# just that demo straight from the stage load. Other arguments go to the
# game: `--demos 0,1,2` plays those demos in turn instead of the random pick,
# `--abort-on-mismatch` fades out when the random byte differs from the
# recording as the original does, `--uncapped` drops the frame limit.
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

# Play one sound effect through the audio backend and exit, by name (a
# case-insensitive SFX_ID name, `_SFX` optional), by number, or as audio/sfx/<either>.
# A music track name (M_INTRO, `M_` optional, or audio/music/<name or number>)
# plays that track in a loop until Ctrl-C; `--play-seconds <n>` stops it after n seconds.
play_sound sound *args:
    ./{{build_dir}}/frigate --play-sound "{{sound}}" {{out_dir}}/goldeneye_audio.o2r {{args}}

# Run the asset-inspection viewer against the packed archives.
viewer *args:
    ./{{build_dir}}/frigate --viewer {{out_dir}}/goldeneye_textures.o2r {{out_dir}}/goldeneye_objects.o2r {{out_dir}}/goldeneye_geometry.o2r {{out_dir}}/goldeneye_audio.o2r {{args}}

# Render textures through the viewer and compare them with the source images.
verify_texture *names:
    python3 tools/verify_texture_render.py {{out_dir}}/goldeneye_textures.o2r {{names}}
