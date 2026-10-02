add_executable(frigate_menu_state_machine_tests MenuStateMachineTest.cpp
                                                 ${CMAKE_SOURCE_DIR}/src/game/MenuStateMachine.cpp
                                                 ${CMAKE_SOURCE_DIR}/src/game/FrameTiming.cpp)
target_include_directories(frigate_menu_state_machine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME MenuStateMachineTest COMMAND frigate_menu_state_machine_tests)

add_executable(frigate_blood_animation_tests BloodAnimationTest.cpp
                                             ${CMAKE_SOURCE_DIR}/src/game/menu/BloodAnimation.cpp)
target_include_directories(frigate_blood_animation_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME BloodAnimationTest COMMAND frigate_blood_animation_tests)

add_executable(frigate_cast_data_tests CastDataTest.cpp ${CMAKE_SOURCE_DIR}/src/game/menu/CastData.cpp)
target_include_directories(frigate_cast_data_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME CastDataTest COMMAND frigate_cast_data_tests)

add_executable(frigate_file_select_tests FileSelectTest.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/FileSelect.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/MenuStateMachine.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/FrameTiming.cpp)
target_link_libraries(frigate_file_select_tests PRIVATE frigate_input_state)
target_include_directories(frigate_file_select_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME FileSelectTest COMMAND frigate_file_select_tests)
