set shell := ["bash", "-euo", "pipefail", "-c"]

build_dir := "build"
out_dir := "archives"

[private]
default:
    @just --list

# Configure and build the desktop viewer.
build type="Release":
    cmake -S. -B{{build_dir}} -DCMAKE_BUILD_TYPE={{type}}
    cmake --build {{build_dir}} --target frigate --parallel

# Build and run the C++ and Python unit tests.
test:
    cmake -S. -B{{build_dir}} -DCMAKE_BUILD_TYPE=Debug
    cmake --build {{build_dir}} --parallel
    ctest --test-dir {{build_dir}} --output-on-failure

# Pack textures, objects and geometry archives from a US ROM into archives/.
pack_archives rom:
    git submodule update --init external/007
    mkdir -p {{out_dir}}
    python3 tools/o2r_packer/build_textures_o2r.py --rom "{{rom}}" --output {{out_dir}}/goldeneye_textures.o2r
    python3 tools/o2r_packer/build_objects_o2r.py --rom "{{rom}}" --output {{out_dir}}/goldeneye_objects.o2r
    python3 tools/o2r_packer/build_geometry_o2r.py --rom "{{rom}}" --output {{out_dir}}/goldeneye_geometry.o2r

# Run the viewer against the packed archives.
run *args:
    ./{{build_dir}}/frigate {{out_dir}}/goldeneye_textures.o2r {{out_dir}}/goldeneye_objects.o2r {{out_dir}}/goldeneye_geometry.o2r {{args}}
