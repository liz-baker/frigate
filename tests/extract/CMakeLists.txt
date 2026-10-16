add_executable(frigate_extract_tests ExtractTest.cpp)
target_link_libraries(frigate_extract_tests PRIVATE frigate_extract)
add_test(NAME ExtractTest COMMAND frigate_extract_tests)

# The ROM-gated checks look for a US ROM in romdata/ when the environment does not already say where to look.
if(NOT DEFINED ENV{FRIGATE_ROMS} AND EXISTS "${CMAKE_SOURCE_DIR}/romdata")
    set_tests_properties(ExtractTest PROPERTIES ENVIRONMENT "FRIGATE_ROMS=${CMAKE_SOURCE_DIR}/romdata")
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
