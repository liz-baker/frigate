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

# The packed audio banks ("GEAB", frigate issue #240) and the music sequence
# table ("GEMS", issue #241): plain data and payload parsers with no
# libultraship dependency.
add_library(frigate_audio_bank STATIC src/audio/AudioBank.cpp src/audio/MusicSequences.cpp)
target_include_directories(frigate_audio_bank PUBLIC src/audio)

# The audio synthesis driver, sound-effect player (frigate issue #240) and
# music players (issue #241): GoldenEye's voice pool run against the microcode
# interpreter. No
# libultraship dependency, so it is desktop-testable on its own.
add_library(frigate_audio_driver STATIC
    src/audio/AudioSynth.cpp
    src/audio/SoundPlayer.cpp
    src/audio/CompactSeq.cpp
    src/audio/CompactSeqPlayer.cpp
    src/audio/MusicPlayer.cpp
    src/audio/AudioEngine.cpp
    src/audio/PcmResampler.cpp
    src/audio/SoundNames.cpp
)
target_include_directories(frigate_audio_driver PUBLIC src/audio)
target_link_libraries(frigate_audio_driver PUBLIC frigate_audio_ucode frigate_audio_bank)

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

# Per-port controller state and its pressed-this-frame edge detection
# (frigate issue #246): no libultraship dependency, so the menu code reads it
# and tests exercise it without a window. The OSContPad side is in
# frigate_platform.
add_library(frigate_input_state STATIC src/platform/InputState.cpp)
target_include_directories(frigate_input_state PUBLIC src)

# Level loading (frigate issue #103): stage tables, the bg header parser and
# the room streaming logic -- no libultraship dependency, so desktop-testable
# on their own. The resource-backed source is part of frigate_game below.
add_library(frigate_level STATIC
    src/level/AiLists.cpp
    src/level/AnimationTable.cpp
    src/level/ApplySpeed.cpp
    src/level/Beams.cpp
    src/level/BulletImpacts.cpp
    src/level/BulletSparks.cpp
    src/level/GlassShards.cpp
    src/model/Quaternion.cpp
    src/model/GameMath.cpp
    src/model/Random.cpp
    src/level/ChrAi.cpp
    src/level/ChrActions.cpp
    src/level/ChrAttack.cpp
    src/level/ChrCombat.cpp
    src/level/ChrCombatTables.cpp
    src/level/ChrFiring.cpp
    src/level/ChrMovement.cpp
    src/level/ColourScreen.cpp
    src/level/PlayerHealth.cpp
    src/level/AttractDemo.cpp
    src/level/LevelInfo.cpp
    src/model/Animation.cpp
    src/model/Playback.cpp
    src/model/RootMotion.cpp
    src/level/BgHeader.cpp
    src/level/BgLevel.cpp
    src/level/BgVisibility.cpp
    src/level/BgView.cpp
    src/level/Inventory.cpp
    src/level/Lang.cpp
    src/level/PickupText.cpp
    src/level/PickupSound.cpp
    src/level/DemoPlayback.cpp
    src/level/Frustum.cpp
    src/level/IntroSection.cpp
    src/game/ChrRecordsParse.cpp
    src/game/Sound.cpp
    src/game/MissionMusic.cpp
    src/game/AiSoundChannels.cpp
    src/level/Chrs.cpp
    src/level/ClippedDoor.cpp
    src/level/Doors.cpp
    src/level/Explosions.cpp
    src/level/Casings.cpp
    src/level/Autogun.cpp
    src/level/Rotor.cpp
    src/level/Cctv.cpp
    src/level/GunKeyframes.cpp
    src/level/GunTick.cpp
    src/level/Interact.cpp
    src/level/MonitorScreen.cpp
    src/level/ObjectDamage.cpp
    src/level/Objects.cpp
    src/level/PlayerMove.cpp
    src/level/PlayerPadInput.cpp
    src/level/PlayerTank.cpp
    src/level/Tank.cpp
    src/level/PlayerWeapons.cpp
    src/level/Objectives.cpp
    src/level/PropLinks.cpp
    src/level/RayTests.cpp
    src/level/PropList.cpp
    src/level/PropModels.cpp
    src/level/Shading.cpp
    src/level/Stan.cpp
    src/level/StanData.cpp
    src/level/WeaponTable.cpp
    src/level/StanIntersection.cpp
    src/level/StageSetup.cpp
    src/level/Vehicles.cpp
    src/level/Waypoints.cpp
)
target_include_directories(frigate_level PUBLIC src)
target_link_libraries(frigate_level PUBLIC frigate_input_state frigate_audio_driver)

if(EXISTS "${CMAKE_SOURCE_DIR}/external/libultraship-wiiu/CMakeLists.txt")
    set(LUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/libultraship-wiiu)

    add_library(frigate_audio_sink STATIC src/audio/AudioSink.cpp src/audio/AudioOutput.cpp)
    target_include_directories(frigate_audio_sink PUBLIC src/audio)
    target_link_libraries(frigate_audio_sink PUBLIC frigate_audio_ucode frigate_audio_driver libultraship)

    # Small graphics-format utilities with no resource-loading dependency of
    # their own (N64 fixed-point matrices, indy ucode opcode numbering) --
    # shared by the viewer, the game loop, and model-drawing code alike.
    add_library(frigate_gfx STATIC src/gfx/FixedMtx.cpp)
    target_include_directories(frigate_gfx PUBLIC src)

    # Runtime resource types for GoldenEye's O2R assets (frigate issue #11) --
    # IResource/ResourceFactory subclasses that deserialize the offline
    # packer's (#10) archive contents. Text/Font are frigate-owned types with
    # no libultraship analog; chr/gun/prop/bg/stan/textures are tracked
    # separately since they should reuse libultraship's own
    # DisplayList/Vertex/Texture resource types (fast/resource/) instead.
    add_library(frigate_resource_types STATIC
        src/model/ModelWalk.cpp
        src/model/JointDrawList.cpp
        src/model/JointDraw.cpp
        src/model/EffectQuads.cpp
        src/model/ModelInstance.cpp
        src/model/RenderState.cpp
        src/model/ModelDraw.cpp
        src/resource/factory/ModelTreeFactory.cpp
        src/resource/type/ModelTree.cpp
        src/resource/factory/ModelSkeletonFactory.cpp
        src/resource/type/ModelSkeleton.cpp
        src/resource/factory/AnimationClipFactory.cpp
        src/resource/type/AnimationClip.cpp
        src/resource/factory/StanFactory.cpp
        src/resource/factory/AudioBankFactory.cpp
        src/resource/type/AudioBankResource.cpp
        src/resource/factory/MusicSequencesFactory.cpp
        src/resource/type/MusicSequencesResource.cpp
        src/resource/type/Stan.cpp
        src/resource/factory/DisplayListFactory.cpp
        src/resource/type/Font.cpp
        src/resource/type/Text.cpp
        src/resource/factory/FontFactory.cpp
        src/resource/factory/TextFactory.cpp
    )
    target_include_directories(frigate_resource_types PUBLIC src)
    target_link_libraries(frigate_resource_types PUBLIC libultraship frigate_tex_expand frigate_gfx frigate_level frigate_audio_bank)

    # Platform-layer bring-up (frigate issue #4) -- wires this port's Context
    # against libultraship's own OS/threading, config, crash-handling,
    # rendering, and input components instead of hand-rolled equivalents. See
    # src/platform/Boot.h for what each piece reuses as-is.
    add_library(frigate_platform STATIC src/platform/Boot.cpp src/platform/Input.cpp)
    target_include_directories(frigate_platform PUBLIC src)
    target_link_libraries(frigate_platform PUBLIC libultraship frigate_resource_types frigate_input_state)

    # `--play-sound`: plays one sound effect through the real audio backend
    # (frigate issue #240).
    add_library(frigate_sound_test STATIC src/audio/SoundTest.cpp)
    target_include_directories(frigate_sound_test PUBLIC src)
    target_link_libraries(frigate_sound_test PUBLIC frigate_audio_sink frigate_audio_driver frigate_resource_types)

    # Throwaway model-viewer glue (see docs/viewer.md); deleting src/viewer/
    # and this target is the whole cleanup.
    add_library(frigate_viewer STATIC
        src/viewer/Camera.cpp
        src/viewer/LevelView.cpp
        src/viewer/ModelStub.cpp
        src/viewer/TextureQuad.cpp
        src/viewer/Viewer.cpp
    )
    target_include_directories(frigate_viewer PUBLIC src)
    target_link_libraries(frigate_viewer PUBLIC frigate_platform frigate_game)

    # The real game loop (frigate issue #81) -- reproduces enough of the
    # GoldenEye decomp's menu dispatcher to drive the boot sequence's menu
    # states (see src/game/menu/).
    add_library(frigate_game STATIC
        src/game/MenuStateMachine.cpp
        src/game/FrameTiming.cpp
        src/game/TextRender.cpp
        src/game/ChrRecords.cpp
        src/gfx/Gu.cpp
        src/game/menu/PropCamera.cpp
        src/game/menu/LegalScreen.cpp
        src/game/menu/NintendoLogo.cpp
        src/game/menu/RarewareLogo.cpp
        src/game/menu/BloodAnimation.cpp
        src/game/menu/EyeIntro.cpp
        src/game/menu/CastData.cpp
        src/game/menu/CastScreen.cpp
        src/game/menu/GoldenEyeLogo.cpp
        src/game/menu/DifficultyModel.cpp
        src/game/menu/DifficultyScreen.cpp
        src/game/menu/FileSelect.cpp
        src/game/menu/FileSelectModel.cpp
        src/game/menu/FolderBackground.cpp
        src/game/menu/FolderPaper.cpp
        src/game/menu/FolderTabs.cpp
        src/game/menu/Options007Model.cpp
        src/game/menu/Options007Screen.cpp
        src/game/menu/FolderScreenSpace.cpp
        src/game/menu/MissionFolders.cpp
        src/game/menu/MissionSelect.cpp
        src/game/menu/MissionSelectModel.cpp
        src/game/menu/FrontCursor.cpp
        src/game/menu/WalletBond.cpp
        src/game/menu/CheatAvailability.cpp
        src/game/menu/WalletBackground.cpp
        src/game/menu/ModeSelect.cpp
        src/game/menu/ModeSelectModel.cpp
        src/game/menu/NoControllers.cpp
        src/game/menu/RunStage.cpp
        src/game/DemoRun.cpp
        src/game/save/SaveData.cpp
        src/game/save/SaveStorage.cpp
        src/game/save/SaveManager.cpp
        src/game/save/SaveProgress.cpp
        src/game/save/GameSettings.cpp
        src/game/save/FileSaveStorage.cpp
        src/game/Game.cpp
        src/game/SoundOutput.cpp
        src/level/EnvironmentTable.cpp
        src/level/Fog.cpp
        src/level/BgApply.cpp
        src/level/BgHit.cpp
        src/level/TextureHits.cpp
        src/level/LightFixtures.cpp
        src/level/RoomLoad.cpp
        src/level/ResourceBgSource.cpp
        src/level/BgRender.cpp
        src/level/ChrDraw.cpp
        src/level/EffectsDraw.cpp
        src/level/DoorGeometry.cpp
        src/level/GunDraw.cpp
        src/level/Hud.cpp
        src/level/ImageDraw.cpp
        src/level/GaitHead.cpp
        src/level/LevelModels.cpp
        src/level/ModelHit.cpp
        src/level/HitSounds.cpp
        src/level/Shots.cpp
        src/level/ShotsProjectile.cpp
        src/level/Projectiles.cpp
        src/level/GunLauncher.cpp
        src/level/ObjectDeform.cpp
        src/level/ProjectilesTick.cpp
        src/level/PlayerCamera.cpp
        src/level/PropFog.cpp
        src/level/PropRender.cpp
        src/level/Sky.cpp
        src/level/ResourceDemoSource.cpp
        src/level/Stage.cpp
        src/level/StageWatch.cpp
        src/level/StageAi.cpp
        src/level/StageAutogun.cpp
        src/level/StageCombat.cpp
        src/level/StageHealth.cpp
        src/level/AutoAim.cpp
        src/level/StageObjectives.cpp
        src/level/StageEffects.cpp
        src/level/StageGun.cpp
        src/level/StageTank.cpp
        src/level/StageTitleFade.cpp
        src/level/Watch.cpp
        src/level/WatchMenu.cpp
        src/level/WatchMenuControl.cpp
        src/level/WatchMenuDraw.cpp
        src/level/WatchMenuItem.cpp
    )
    target_include_directories(frigate_game PUBLIC src)
    target_link_libraries(frigate_game PUBLIC frigate_platform frigate_level frigate_audio_sink)

    add_executable(frigate src/main.cpp)
    target_link_libraries(frigate PRIVATE frigate_platform frigate_viewer frigate_game frigate_sound_test)

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

# The game's arithmetic rounds every float operation on its own, as the
# console's FPU does. A compiler that fuses a multiply with the add after it
# (the default on ARM, PowerPC and some Clang targets) changes results the
# demos' recorded random numbers depend on.
if(NOT MSVC)
    foreach(frigate_logic_target frigate_level frigate_game frigate_viewer frigate_gfx)
        if(TARGET ${frigate_logic_target})
            target_compile_options(${frigate_logic_target} PRIVATE -ffp-contract=off)
        endif()
    endforeach()
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
    add_subdirectory(tests/game)
    add_subdirectory(tests/level)
    add_subdirectory(tests/platform)

    # Desktop-testable resource-type tests need a real libultraship (headers
    # for Ship::BinaryReader/File/etc.), same requirement as
    # frigate_resource_types itself above.
    if(TARGET frigate_resource_types)
        add_subdirectory(tests/resource)
    endif()

    # `frigate --play-sound` end to end, into SDL's disk audio driver.
    if(TARGET frigate)
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_FOUND)
            add_test(
                NAME SoundTestEndToEnd
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/audio/sound_test_e2e.py
                        $<TARGET_FILE:frigate> ${CMAKE_SOURCE_DIR}/tools/o2r_packer
            )
        endif()
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
            NAME PackRarewareLogoTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_pack_rarewarelogo -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME GeAudioTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_ge_audio -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME PackAudioTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_pack_audio -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME PackEyeIntroTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_pack_eyeintro -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME PackChrRecordsTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_pack_chr_records -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME PackAiListsTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_pack_ai_lists -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME PackWeaponStatsTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_pack_weapon_stats -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME PackPropModelsTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_pack_prop_models -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/o2r_packer
        )
        add_test(
            NAME PackSkyImagesTest
            COMMAND ${Python3_EXECUTABLE} -m unittest test_pack_sky_images -v
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
