add_executable(frigate_input_state_tests InputStateTest.cpp)
target_link_libraries(frigate_input_state_tests PRIVATE frigate_input_state)

add_test(NAME InputStateTest COMMAND frigate_input_state_tests)

add_executable(frigate_input_script_tests InputScriptTest.cpp)
target_link_libraries(frigate_input_script_tests PRIVATE frigate_input_state)
add_test(NAME InputScriptTest COMMAND frigate_input_script_tests)

add_executable(frigate_port_assignment_tests PortAssignmentTest.cpp)
target_link_libraries(frigate_port_assignment_tests PRIVATE frigate_input_state)
add_test(NAME PortAssignmentTest COMMAND frigate_port_assignment_tests)

add_executable(frigate_rumble_tests RumbleTest.cpp)
target_link_libraries(frigate_rumble_tests PRIVATE frigate_input_state)
add_test(NAME RumbleTest COMMAND frigate_rumble_tests)

# Pad handling needs libultraship's OSContPad and ControlDeck types, not a window.
if(TARGET frigate_platform)
    add_executable(frigate_input_pad_tests InputPadTest.cpp)
    target_link_libraries(frigate_input_pad_tests PRIVATE frigate_platform)

    add_test(NAME InputPadTest COMMAND frigate_input_pad_tests)

    add_executable(frigate_bindings_apply_tests BindingsApplyTest.cpp)
    target_link_libraries(frigate_bindings_apply_tests PRIVATE frigate_platform)
    add_test(NAME BindingsApplyTest COMMAND frigate_bindings_apply_tests)
endif()

add_executable(frigate_modern_input_tests ModernInputTest.cpp)
target_link_libraries(frigate_modern_input_tests PRIVATE frigate_input_state)
add_test(NAME ModernInputTest COMMAND frigate_modern_input_tests)

add_executable(frigate_bindings_tests BindingsTest.cpp)
target_link_libraries(frigate_bindings_tests PRIVATE frigate_input_state)
add_test(NAME BindingsTest COMMAND frigate_bindings_tests)

add_executable(frigate_menu_pointer_tests MenuPointerTest.cpp)
target_link_libraries(frigate_menu_pointer_tests PRIVATE frigate_input_state)
add_test(NAME MenuPointerTest COMMAND frigate_menu_pointer_tests)

# The Wii U allocator's pooling on the host: the source is compiled in directly, which replaces the
# global operator new and delete for this program.
add_executable(frigate_wiiu_allocator_tests WiiUAllocatorTest.cpp ${CMAKE_SOURCE_DIR}/src/platform/WiiUAllocator.cpp)
target_include_directories(frigate_wiiu_allocator_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(frigate_wiiu_allocator_tests PRIVATE FRIGATE_ALLOCATOR_HOST_TEST)
find_package(Threads REQUIRED)
target_link_libraries(frigate_wiiu_allocator_tests PRIVATE Threads::Threads)
add_test(NAME WiiUAllocatorTest COMMAND frigate_wiiu_allocator_tests)
