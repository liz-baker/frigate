#!/usr/bin/env bash
# Builds the libraries frigate links on macOS as universal (arm64 + x86_64)
# dylibs into a prefix. Homebrew can't supply them: it ships one architecture
# only. The release workflow runs this, then builds frigate against the prefix.
#
#   scripts/macos-universal-deps.sh <prefix>
set -euo pipefail

prefix=${1:?usage: macos-universal-deps.sh <prefix>}
work=$(mktemp -d)
mkdir -p "$prefix"
prefix=$(cd "$prefix" && pwd)

common=(
    -GNinja
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
    "-DCMAKE_INSTALL_PREFIX=$prefix"
    "-DCMAKE_PREFIX_PATH=$prefix"
    # Homebrew's single-architecture copies must not be picked up.
    -DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew
    -DBUILD_SHARED_LIBS=ON
    # Some dependencies still declare a CMake minimum CMake 4 refuses.
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

build() { # name source-dir [extra cmake args...]
    local name=$1 src=$2
    shift 2
    cmake -S "$src" -B "$work/$name-build" "${common[@]}" "$@"
    cmake --build "$work/$name-build" --parallel
    cmake --install "$work/$name-build"
}

fetch() { # url dest-dir
    mkdir -p "$2"
    curl -fsSL "$1" | tar -xz -C "$2" --strip-components=1
}

fetch https://github.com/libsdl-org/SDL/releases/download/release-3.4.12/SDL3-3.4.12.tar.gz "$work/sdl"
build sdl "$work/sdl" -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF

fetch https://github.com/leethomason/tinyxml2/archive/refs/tags/10.0.0.tar.gz "$work/tinyxml2"
build tinyxml2 "$work/tinyxml2" -Dtinyxml2_BUILD_TESTING=OFF

fetch https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz "$work/json"
build json "$work/json" -DJSON_BuildTests=OFF

fetch https://github.com/nih-at/libzip/releases/download/v1.11.3/libzip-1.11.3.tar.gz "$work/libzip"
build libzip "$work/libzip" -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF \
    -DENABLE_COMMONCRYPTO=OFF -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF -DENABLE_OPENSSL=OFF \
    -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF

fetch https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.tgz "$work/glew"
build glew "$work/glew/build/cmake" -DBUILD_UTILS=OFF

rm -rf "$work"
