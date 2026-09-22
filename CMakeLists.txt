# libultraship itself requires 3.24; declared here too so a too-old
# cmake fails fast on this file rather than obscurely inside the submodule.
cmake_minimum_required(VERSION 3.24)
project(frigate CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Software interpreter for GE's RSP audio microcode (frigate issue #3) --
# self-contained, no dependency on libultraship or the decomp, so it
# builds and is desktop-testable on its own even before either is wired in.
add_library(frigate_audio_ucode STATIC
    src/audio/AudioUcode.cpp
)
target_include_directories(frigate_audio_ucode PUBLIC src/audio)

# AudioSink bridges AudioUcode's output to libultraship's AudioPlayer
# (the native AX backend on Wii U, or whichever backend is active
# elsewhere). It links against the real `libultraship` target, so it needs
# that submodule checked out (`git submodule update --init
# external/libultraship-wiiu`); building it pulls in libultraship's
# own CMakeLists (and its dependencies -- SDL3, tinyxml2, etc. -- see
# .github/workflows for what CI installs).
# G_SETTEX rewrite (frigate issue #26) -- pure command-word logic with no
# libultraship dependency, so it is desktop-testable on its own.
add_library(frigate_tex_expand STATIC src/texture/TexExpand.cpp)
target_include_directories(frigate_tex_expand PUBLIC src)

if(EXISTS "${CMAKE_SOURCE_DIR}/external/libultraship-wiiu/CMakeLists.txt")
    set(LUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/libultraship-wiiu)

    add_library(frigate_audio_sink STATIC src/audio/AudioSink.cpp)
    target_include_directories(frigate_audio_sink PUBLIC src/audio)
    target_link_libraries(frigate_audio_sink PUBLIC frigate_audio_ucode libultraship)

    # Runtime resource types for GoldenEye's O2R assets (frigate issue #11) --
    # IResource/ResourceFactory subclasses that deserialize the offline
    # packer's (#10) archive contents. Text/Font are frigate-owned types with
    # no libultraship analog; chr/gun/prop/bg/stan/textures are tracked
    # separately since they should reuse libultraship's own
    # DisplayList/Vertex/Texture resource types (fast/resource/) instead.
    add_library(frigate_resource_types STATIC
        src/model/ModelWalk.cpp
        src/model/Animation.cpp
        src/resource/factory/ModelTreeFactory.cpp
        src/resource/type/ModelTree.cpp
        src/resource/factory/ModelSkeletonFactory.cpp
        src/resource/type/ModelSkeleton.cpp
        src/resource/factory/AnimationClipFactory.cpp
        src/resource/type/AnimationClip.cpp
        src/resource/factory/DisplayListFactory.cpp
        src/resource/type/Font.cpp
        src/resource/type/Text.cpp
        src/resource/factory/FontFactory.cpp
        src/resource/factory/TextFactory.cpp
    )
    target_include_directories(frigate_resource_types PUBLIC src)
    target_link_libraries(frigate_resource_types PUBLIC libultraship frigate_tex_expand)

    # Platform-layer bring-up (frigate issue #4) -- wires this port's Context
    # against libultraship's own OS/threading, config, crash-handling,
    # rendering, and input components instead of hand-rolled equivalents. See
    # src/platform/Boot.h for what each piece reuses as-is.
    add_library(frigate_platform STATIC src/platform/Boot.cpp)
    target_include_directories(frigate_platform PUBLIC src)
    target_link_libraries(frigate_platform PUBLIC libultraship frigate_resource_types)

    # Throwaway model-viewer glue (see docs/viewer.md); deleting src/viewer/
    # and this target is the whole cleanup.
    add_library(frigate_viewer STATIC
        src/viewer/Camera.cpp
        src/viewer/ModelStub.cpp
        src/viewer/TextureQuad.cpp
        src/viewer/Viewer.cpp
    )
    target_include_directories(frigate_viewer PUBLIC src)
    target_link_libraries(frigate_viewer PUBLIC frigate_platform)

    add_executable(frigate src/main.cpp)
    target_link_libraries(frigate PRIVATE frigate_platform frigate_viewer)

    # The rendering backends load their shaders from an archive at draw time
    # and abort if it's missing, so the executable ships with one alongside it.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set_target_properties(frigate PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    set(LUS_SHADER_DIR "${CMAKE_SOURCE_DIR}/external/libultraship-wiiu/src/fast/shaders")
    file(GLOB_RECURSE LUS_SHADER_SOURCES CONFIGURE_DEPENDS "${LUS_SHADER_DIR}/*")
    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/f3d.o2r"
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/o2r_packer/build_f3d_o2r.py
                --shaders ${LUS_SHADER_DIR} --output "${CMAKE_BINARY_DIR}/f3d.o2r"
        DEPENDS ${LUS_SHADER_SOURCES}
                ${CMAKE_SOURCE_DIR}/tools/o2r_packer/build_f3d_o2r.py
                ${CMAKE_SOURCE_DIR}/tools/o2r_packer/o2r_writer.py
        VERBATIM
    )
    add_custom_target(frigate_f3d_archive DEPENDS "${CMAKE_BINARY_DIR}/f3d.o2r")
    add_dependencies(frigate frigate_f3d_archive)
else()
    message(STATUS "external/libultraship-wiiu not checked out; skipping frigate_audio_sink/frigate_platform/frigate_resource_types targets")
endif()

# Skipped on iOS: Xcode's generator requires a PRODUCT_BUNDLE_IDENTIFIER for
# any target when CMAKE_SYSTEM_NAME is iOS, which a plain CLI test
# executable doesn't have and doesn't need -- these tests aren't run
# on-device, only by test-validation's Linux/Windows jobs.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_testing()
    add_subdirectory(tests/audio)
    add_subdirectory(tests/texture)
    add_subdirectory(tests/model)

    # Desktop-testable resource-type tests need a real libultraship (headers
    # for Ship::BinaryReader/File/etc.), same requirement as
    # frigate_resource_types itself above.
    if(TARGET frigate_resource_types)
        add_subdirectory(tests/resource)
    endif()

    # o2r_writer.py (frigate issue #10) is a host-side offline tool, not
    # part of the console build -- no ROM or extracted assets needed to
    # test it, only a Python3 interpreter on the build host.
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(
            NAME O2rWriterTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_o2r_writer -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME PackTexturesTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_pack_textures -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME VerifyRoundtripTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_verify_roundtrip -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME BuildTexturesO2rTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_build_textures_o2r -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME DecompExtractTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_decomp_extract -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME PackObjectsTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_pack_objects -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME BuildObjectsO2rTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_build_objects_o2r -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME PackGeometryTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_pack_geometry -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME BuildF3dO2rTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_build_f3d_o2r -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME BuildGeometryO2rTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_build_geometry_o2r -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME ModelTreeTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_ge_modeltree test_ge_dlemit -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
    else()
        message(STATUS "Python3 interpreter not found; skipping o2r_packer tests (see tools/o2r_packer/README.md)")
    endif()
endif()
