add_executable(frigate_menu_state_machine_tests MenuStateMachineTest.cpp
                                                 ${CMAKE_SOURCE_DIR}/src/game/MenuStateMachine.cpp)
target_include_directories(frigate_menu_state_machine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME MenuStateMachineTest COMMAND frigate_menu_state_machine_tests)

add_executable(frigate_blood_animation_tests BloodAnimationTest.cpp
                                             ${CMAKE_SOURCE_DIR}/src/game/menu/BloodAnimation.cpp)
target_include_directories(frigate_blood_animation_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME BloodAnimationTest COMMAND frigate_blood_animation_tests)
