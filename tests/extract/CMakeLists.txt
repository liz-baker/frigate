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
    # --allow-missing-in-ext makes reference members outside tests/extract/covered_prefixes.txt pending; the
    # extraction ports add their prefixes there, and the last one drops this option so everything is strict.
    add_test(
        NAME VerifyExtract
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/verify_extract.py
                $<TARGET_FILE:frigate-extract> ${CMAKE_SOURCE_DIR} --allow-missing-in-ext
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
