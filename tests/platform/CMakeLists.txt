add_executable(frigate_input_state_tests InputStateTest.cpp)
target_link_libraries(frigate_input_state_tests PRIVATE frigate_input_state)

add_test(NAME InputStateTest COMMAND frigate_input_state_tests)

# Pad handling needs libultraship's OSContPad and ControlDeck types, not a window.
if(TARGET frigate_platform)
    add_executable(frigate_input_pad_tests InputPadTest.cpp)
    target_link_libraries(frigate_input_pad_tests PRIVATE frigate_platform)

    add_test(NAME InputPadTest COMMAND frigate_input_pad_tests)
endif()
