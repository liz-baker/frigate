cmake_minimum_required(VERSION 3.16)
project(frigate CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Software interpreter for GE's RSP audio microcode (frigate issue #3) --
# self-contained, no dependency on the decomp or libultraship-wiiu, so it
# builds and is desktop-testable on its own ahead of the rest of this
# repo's build wiring (roadmap item 3).
add_library(frigate_audio_ucode STATIC
    src/audio/AudioUcode.cpp
)
target_include_directories(frigate_audio_ucode PUBLIC src/audio)

# AudioSink bridges AudioUcode's output to libultraship-wiiu's AudioPlayer
# (the native AX backend on Wii U, or whichever backend is active
# elsewhere). It needs that submodule checked out
# (`git submodule update --init external/libultraship-wiiu`) and, since
# this repo doesn't build libultraship-wiiu itself yet, only compiles here
# -- it isn't linked into anything until roadmap item 3 wires the rest of
# the build against it.
set(LUS_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/libultraship-wiiu/include")
if(EXISTS "${LUS_INCLUDE_DIR}/ship/audio/Audio.h")
    add_library(frigate_audio_sink STATIC src/audio/AudioSink.cpp)
    target_include_directories(frigate_audio_sink PUBLIC src/audio PRIVATE "${LUS_INCLUDE_DIR}")
    target_link_libraries(frigate_audio_sink PUBLIC frigate_audio_ucode)
else()
    message(STATUS "external/libultraship-wiiu not checked out; skipping frigate_audio_sink target (see src/audio/AudioSink.h)")
endif()

enable_testing()
add_subdirectory(tests/audio)
