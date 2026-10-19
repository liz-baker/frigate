add_executable(frigate_extract_tests ExtractTest.cpp)
target_link_libraries(frigate_extract_tests PRIVATE frigate_extract)
add_test(NAME ExtractTest COMMAND frigate_extract_tests)

# The ROM-gated checks look for a US ROM in romdata/ when the environment does not already say where to look.
if(NOT DEFINED ENV{FRIGATE_ROMS} AND EXISTS "${CMAKE_SOURCE_DIR}/romdata")
    set_tests_properties(ExtractTest PROPERTIES ENVIRONMENT "FRIGATE_ROMS=${CMAKE_SOURCE_DIR}/romdata")
endif()

add_executable(frigate_extract_texture_tests TextureTest.cpp)
target_link_libraries(frigate_extract_texture_tests PRIVATE frigate_extract)
add_test(NAME ExtractTextureTest COMMAND frigate_extract_texture_tests)
if(NOT DEFINED ENV{FRIGATE_ROMS} AND EXISTS "${CMAKE_SOURCE_DIR}/romdata")
    set_tests_properties(ExtractTextureTest PROPERTIES ENVIRONMENT "FRIGATE_ROMS=${CMAKE_SOURCE_DIR}/romdata")
endif()

add_executable(frigate_extract_audio_tests AudioTest.cpp)
target_link_libraries(frigate_extract_audio_tests PRIVATE frigate_extract)
add_test(NAME ExtractAudioTest COMMAND frigate_extract_audio_tests)
if(NOT DEFINED ENV{FRIGATE_ROMS} AND EXISTS "${CMAKE_SOURCE_DIR}/romdata")
    set_tests_properties(ExtractAudioTest PROPERTIES ENVIRONMENT "FRIGATE_ROMS=${CMAKE_SOURCE_DIR}/romdata")
endif()

add_executable(frigate_extract_model_tests ModelTest.cpp)
target_link_libraries(frigate_extract_model_tests PRIVATE frigate_extract)
add_test(NAME ExtractModelTest COMMAND frigate_extract_model_tests)
if(NOT DEFINED ENV{FRIGATE_ROMS} AND EXISTS "${CMAKE_SOURCE_DIR}/romdata")
    set_tests_properties(ExtractModelTest PROPERTIES ENVIRONMENT "FRIGATE_ROMS=${CMAKE_SOURCE_DIR}/romdata")
endif()

add_executable(frigate_pack_shaders_tests PackShadersTest.cpp ${CMAKE_SOURCE_DIR}/tools/pack_shaders/PackShaders.cpp)
target_include_directories(frigate_pack_shaders_tests PRIVATE ${CMAKE_SOURCE_DIR}/tools/pack_shaders)
target_link_libraries(frigate_pack_shaders_tests PRIVATE frigate_extract)
add_test(NAME PackShadersTest COMMAND frigate_pack_shaders_tests)

add_executable(frigate_extract_writer_fixture WriterFixture.cpp)
target_link_libraries(frigate_extract_writer_fixture PRIVATE frigate_extract)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(
        NAME ExtractWriterEquivalence
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/writer_equivalence.py
                $<TARGET_FILE:frigate_extract_writer_fixture> ${CMAKE_SOURCE_DIR}/tools/o2r_packer
    )

    # The Python packers' archives against frigate-extract's, for the US ROM (docs/extract-verification.md).
    # Strict: every reference member must be in the extractor's archives and identical.
    add_test(
        NAME VerifyExtract
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/verify_extract.py
                $<TARGET_FILE:frigate-extract> ${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(VerifyExtract PROPERTIES SKIP_RETURN_CODE 77)
    if(NOT DEFINED ENV{FRIGATE_ROMS} AND EXISTS "${CMAKE_SOURCE_DIR}/romdata")
        set_tests_properties(VerifyExtract PROPERTIES ENVIRONMENT "FRIGATE_ROMS=${CMAKE_SOURCE_DIR}/romdata")
    endif()
endif()

add_executable(frigate_extract_object_table_tests ObjectTablesTest.cpp)
target_link_libraries(frigate_extract_object_table_tests PRIVATE frigate_extract)
add_test(NAME ExtractObjectTablesTest COMMAND frigate_extract_object_table_tests)
if(NOT DEFINED ENV{FRIGATE_ROMS} AND EXISTS "${CMAKE_SOURCE_DIR}/romdata")
    set_tests_properties(ExtractObjectTablesTest PROPERTIES ENVIRONMENT "FRIGATE_ROMS=${CMAKE_SOURCE_DIR}/romdata")
endif()

if(Python3_FOUND)
    # The table generators on synthetic sources; the check against external/007 skips without that checkout.
    add_test(
        NAME GenTablesTest
        COMMAND ${Python3_EXECUTABLE} -m unittest test_gen_tables -v
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools
    )
endif()

# The extractor's texture decoder against the game's own decompression code, built from the decomp at build time
# (docs/extract-verification.md). Needs the decomp checkout, python3 and a C compiler; otherwise the check is not built.
include(CheckLanguage)
check_language(C)
if(Python3_FOUND AND CMAKE_C_COMPILER AND NOT MSVC AND EXISTS "${CMAKE_SOURCE_DIR}/external/007/src/game/image.c")
    enable_language(C)
    set(GAME_TEXTURE_DECODER "${CMAKE_CURRENT_BINARY_DIR}/game_texture_decoder.inc")
    add_custom_command(
        OUTPUT ${GAME_TEXTURE_DECODER}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gen_game_texture_decoder.py
                ${CMAKE_SOURCE_DIR}/external/007 ${GAME_TEXTURE_DECODER}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_game_texture_decoder.py
                ${CMAKE_SOURCE_DIR}/external/007/src/game/image.c
                ${CMAKE_SOURCE_DIR}/external/007/src/game/image_bank.c
                ${CMAKE_SOURCE_DIR}/external/007/src/game/image.h
    )
    add_library(frigate_game_texture_oracle STATIC GameTextureOracle.c ${GAME_TEXTURE_DECODER})
    target_include_directories(frigate_game_texture_oracle PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                               PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    # The decomp's code is compiled as written; its warnings are not ours to fix.
    target_compile_options(frigate_game_texture_oracle PRIVATE -w)

    add_executable(frigate_extract_texture_oracle_tests TextureOracleTest.cpp)
    target_link_libraries(frigate_extract_texture_oracle_tests PRIVATE frigate_extract frigate_game_texture_oracle)
    add_test(NAME ExtractTextureOracleTest COMMAND frigate_extract_texture_oracle_tests)
    set_tests_properties(ExtractTextureOracleTest PROPERTIES SKIP_RETURN_CODE 77)
    if(NOT DEFINED ENV{FRIGATE_ROMS} AND EXISTS "${CMAKE_SOURCE_DIR}/romdata")
        set_tests_properties(ExtractTextureOracleTest PROPERTIES ENVIRONMENT "FRIGATE_ROMS=${CMAKE_SOURCE_DIR}/romdata")
    endif()
endif()

add_executable(frigate_first_run_tests FirstRunTest.cpp)
target_link_libraries(frigate_first_run_tests PRIVATE frigate_extract)
add_test(NAME FirstRunTest COMMAND frigate_first_run_tests)
if(NOT DEFINED ENV{FRIGATE_ROMS} AND EXISTS "${CMAKE_SOURCE_DIR}/romdata")
    set_tests_properties(FirstRunTest PROPERTIES ENVIRONMENT "FRIGATE_ROMS=${CMAKE_SOURCE_DIR}/romdata")
endif()
