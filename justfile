set shell := ["bash", "-euo", "pipefail", "-c"]

build_dir := "build"
out_dir := "archives"

[private]
default:
    @just --list

# Configure and build the desktop viewer/game. Debug by default -- pass
# `type=Release` for an optimized build once things are working.
build type="Debug":
    cmake -S. -B{{build_dir}} -DCMAKE_BUILD_TYPE={{type}}
    cmake --build {{build_dir}} --target frigate --parallel

# Build and run the C++ and Python unit tests.
test:
    cmake -S. -B{{build_dir}} -DCMAKE_BUILD_TYPE=Debug
    cmake --build {{build_dir}} --parallel
    ctest --test-dir {{build_dir}} --output-on-failure

# Pack textures, objects, geometry and audio archives from a US ROM into archives/.
pack_archives rom:
    git submodule update --init external/007
    mkdir -p {{out_dir}}
    python3 tools/o2r_packer/build_textures_o2r.py --rom "{{rom}}" --output {{out_dir}}/goldeneye_textures.o2r
    python3 tools/o2r_packer/build_objects_o2r.py --rom "{{rom}}" --output {{out_dir}}/goldeneye_objects.o2r
    python3 tools/o2r_packer/build_geometry_o2r.py --rom "{{rom}}" --output {{out_dir}}/goldeneye_geometry.o2r
    python3 tools/o2r_packer/build_audio_o2r.py --rom "{{rom}}" --output {{out_dir}}/goldeneye_audio.o2r

# Draw the briefcase gun, textured (five paletted images).
demo:
    just viewer --model models/gun/GbriefcaseZ --distance 300

# Run the game (needs archives/ -- see pack_archives). `--input-script
# "300-302:A,500-502:DD+X=-70"` presses buttons on given frames in place of
# the controller (see src/platform/InputScript.h), so a headless run can drive
# the menus.
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
