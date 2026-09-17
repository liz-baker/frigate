# libultraship-wiiu itself requires 3.24; declared here too so a too-old
# cmake fails fast on this file rather than obscurely inside the submodule.
cmake_minimum_required(VERSION 3.24)
project(frigate CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Software interpreter for GE's RSP audio microcode (frigate issue #3) --
# self-contained, no dependency on libultraship-wiiu or the decomp, so it
# builds and is desktop-testable on its own even before either is wired in.
add_library(frigate_audio_ucode STATIC
    src/audio/AudioUcode.cpp
)
target_include_directories(frigate_audio_ucode PUBLIC src/audio)

# AudioSink bridges AudioUcode's output to libultraship-wiiu's AudioPlayer
# (the native AX backend on Wii U, or whichever backend is active
# elsewhere). It links against the real `libultraship` target, so it needs
# that submodule checked out (`git submodule update --init
# external/libultraship-wiiu`); building it pulls in libultraship-wiiu's
# own CMakeLists (and its dependencies -- SDL3, tinyxml2, etc. -- see
# .github/workflows for what CI installs).
if(EXISTS "${CMAKE_SOURCE_DIR}/external/libultraship-wiiu/CMakeLists.txt")
    set(LUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/libultraship-wiiu)

    add_library(frigate_audio_sink STATIC src/audio/AudioSink.cpp)
    target_include_directories(frigate_audio_sink PUBLIC src/audio)
    target_link_libraries(frigate_audio_sink PUBLIC frigate_audio_ucode libultraship)
else()
    message(STATUS "external/libultraship-wiiu not checked out; skipping frigate_audio_sink target (see src/audio/AudioSink.h)")
endif()

# Skipped on iOS: Xcode's generator requires a PRODUCT_BUNDLE_IDENTIFIER for
# any target when CMAKE_SYSTEM_NAME is iOS, which a plain CLI test
# executable doesn't have and doesn't need -- these tests aren't run
# on-device, only by test-validation's Linux/Windows jobs.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_testing()
    add_subdirectory(tests/audio)
endif()
